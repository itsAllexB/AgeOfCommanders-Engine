/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 Age of Commanders contributors
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// AoC F14a: the pure parts of save fidelity (design D11 sections 1.3 and 3.1): the logic random generator's state round
// trip (live, through its save block, byte layout, CRC word), the slot matching and the heap check that restore the
// sleepy-update heap, and the layout's save and restore on a small synthetic heap. GameLogic's own heap functions are
// private members working on UpdateModule objects inside a running game, so they cannot run here; the synthetic heap
// below is a model of their sift rules (GameLogic::rebalanceParentSleepyUpdate, rebalanceChildSleepyUpdate,
// popSleepyUpdate, remakeSleepyUpdate: strict comparisons, the left child on a tie), and the game gates cover the real
// ones.

#include <stdio.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

#include "AoC/AoCSaveFidelity.h"
#include "Common/GameCommon.h"
#include "Common/RandomValue.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/LogicRandomValue.h"

namespace
{

std::vector<unsigned char> readFile( const std::string &path )
{
	std::vector<unsigned char> bytes;
	FILE *f = fopen(path.c_str(), "rb");
	if (f == nullptr)
		return bytes;
	int c;
	while ((c = fgetc(f)) != EOF)
		bytes.push_back((unsigned char)c);
	fclose(f);
	return bytes;
}

std::vector<Int> draws( Int count )
{
	std::vector<Int> v;
	for (Int i = 0; i < count; ++i)
		v.push_back(GameLogicRandomValue(0, 1000000));
	return v;
}

// --- a model of the scheduler's heap (GameLogic.cpp), entries (key, priority) ----------------------------------------
struct Entry
{
	AoCSleepySlot key;
	UnsignedInt priority;
};

class ModelHeap
{
public:
	std::vector<Entry> a;

	void push( const Entry &e )
	{
		a.push_back(e);
		siftUp((Int)a.size() - 1);
	}
	Entry pop()
	{
		Entry top = a[0];
		a[0] = a.back();
		a.pop_back();
		if (!a.empty())
			siftDown(0);
		return top;
	}
	void remake()
	{
		for (Int parent = (Int)a.size() / 2; parent >= 0; --parent)
			siftDown(parent);
	}
	std::vector<UnsignedInt> priorities() const
	{
		std::vector<UnsignedInt> p;
		for (size_t i = 0; i < a.size(); ++i)
			p.push_back(a[i].priority);
		return p;
	}
	std::vector<AoCSleepySlot> keys() const
	{
		std::vector<AoCSleepySlot> k;
		for (size_t i = 0; i < a.size(); ++i)
			k.push_back(a[i].key);
		return k;
	}

private:
	static Bool lower( const Entry &x, const Entry &y ) { return x.priority > y.priority; }	// isLowerPriority
	void siftUp( Int i )
	{
		Int parent = ((i + 1) >> 1) - 1;
		while (parent >= 0 && lower(a[parent], a[i]))
		{
			std::swap(a[parent], a[i]);
			i = parent;
			parent = ((parent + 1) >> 1) - 1;
		}
	}
	void siftDown( Int i )
	{
		const Int sz = (Int)a.size();
		Int child = (i << 1) + 1;
		while (child < sz)
		{
			if (child < sz - 1 && lower(a[child], a[child + 1]))
				++child;
			if (!lower(a[i], a[child]))
				break;
			std::swap(a[i], a[child]);
			i = child;
			child = (i << 1) + 1;
		}
	}
};

/// Pops every entry due at or before 'frame' (priority = frame << 2 | phase), as one logic frame does, and returns the
/// object ids in the order they ran; each is re-pushed 'sleep' frames later.
std::vector<Int> runFrame( ModelHeap &h, UnsignedInt frame, UnsignedInt sleep )
{
	std::vector<Int> ran;
	std::vector<Entry> again;
	while (!h.a.empty() && (h.a[0].priority >> 2) <= frame)
	{
		Entry e = h.pop();
		ran.push_back((Int)e.key.objectID);
		e.priority = ((frame + sleep) << 2) | (e.priority & 3);
		again.push_back(e);
	}
	for (size_t i = 0; i < again.size(); ++i)
		h.push(again[i]);
	return ran;
}

} // namespace

// Decision 1: the state read from the live generator and applied again replays the same draws, and the base seed (the
// replay header's seed) comes back with it.
TEST(AoCSaveFidelityTest, GeneratorStateReplaysTheSameDraws)
{
	InitRandom(12345);
	draws(7);
	const AoCLogicRandomState saved = AoCLogicRandomState::live();
	EXPECT_EQ(saved.baseSeed, 12345u);
	EXPECT_EQ(saved.seedCRC(), GetGameLogicRandomSeedCRC()) << "the CRC word of the per-frame CRC";
	const std::vector<Int> first = draws(50);
	EXPECT_FALSE(AoCLogicRandomState::live() == saved) << "drawing advances the generator";

	InitRandom(999);	// what a loaded game holds: another seed and other draws
	draws(3);
	saved.apply();
	EXPECT_TRUE(AoCLogicRandomState::live() == saved);
	EXPECT_EQ(GetGameLogicRandomSeed(), 12345u);
	EXPECT_EQ(GetGameLogicRandomSeedCRC(), saved.seedCRC());
	EXPECT_EQ(draws(50), first);
}

// Decision 1: the save block is the six words and then the base seed (28 bytes), and a state read back through it
// replays the same draws as the state that was written.
TEST(AoCSaveFidelityTest, GeneratorStateRoundTripsThroughItsSaveBlock)
{
	const std::string path = ::testing::TempDir() + "AoCSaveFidelityRandom.sav";
	InitRandom(42);
	draws(11);
	AoCLogicRandomState written = AoCLogicRandomState::live();
	{
		XferSave save;
		save.open(path.c_str());
		written.xfer(&save);
		save.close();
	}
	const std::vector<Int> expected = draws(40);
	const std::vector<unsigned char> bytes = readFile(path);
	ASSERT_EQ(bytes.size(), (size_t)(7 * sizeof(UnsignedInt)));
	for (Int i = 0; i < AOC_LOGIC_RANDOM_WORDS; ++i)
	{
		UnsignedInt word = 0;
		memcpy(&word, &bytes[i * sizeof(UnsignedInt)], sizeof(word));
		EXPECT_EQ(word, written.seed[i]) << "word " << i;
	}
	UnsignedInt base = 0;
	memcpy(&base, &bytes[6 * sizeof(UnsignedInt)], sizeof(base));
	EXPECT_EQ(base, 42u) << "the base seed last";

	AoCLogicRandomState loaded;
	{
		XferLoad load;
		load.open(path.c_str());
		loaded.xfer(&load);
		load.close();
	}
	EXPECT_TRUE(loaded == written);
	InitRandom(7);
	loaded.apply();
	EXPECT_EQ(draws(40), expected);
	remove(path.c_str());
}

// The AI groups block (AIGroup and AI save nothing in stock): the id counter, then each group's id, speed, dirty bit and
// members in order, back unchanged through the save block; an empty list is just the counter and a zero count.
TEST(AoCSaveFidelityTest, GroupsRoundTripThroughTheirSaveBlock)
{
	const std::string path = ::testing::TempDir() + "AoCSaveFidelityGroups.sav";
	std::vector<AoCSavedGroup> written(3);
	written[0].id = 7; written[0].speed = 22.5f; written[0].dirty = FALSE;
	written[0].members.push_back((ObjectID)41); written[0].members.push_back((ObjectID)38); written[0].members.push_back((ObjectID)45);
	written[1].id = 9; written[1].speed = 0.0f; written[1].dirty = TRUE;
	written[1].members.push_back((ObjectID)50);
	written[2].id = 347; written[2].speed = 30.0f; written[2].dirty = FALSE;
	UnsignedInt last = 347;
	{
		XferSave save;
		save.open(path.c_str());
		AoCSaveFidelity::xferGroups(&save, last, written);
		save.close();
	}
	EXPECT_EQ(readFile(path).size(), 2 * sizeof(UnsignedInt) + 3 * (sizeof(UnsignedInt) + sizeof(Real) + sizeof(Bool) + sizeof(UnsignedInt)) + 4 * sizeof(ObjectID));

	std::vector<AoCSavedGroup> loaded(1);	// replaced, not appended to
	UnsignedInt loadedLast = 0;
	{
		XferLoad load;
		load.open(path.c_str());
		AoCSaveFidelity::xferGroups(&load, loadedLast, loaded);
		load.close();
	}
	EXPECT_EQ(loadedLast, 347u);
	ASSERT_EQ(loaded.size(), 3u);
	for (size_t i = 0; i < 3; ++i)
	{
		EXPECT_EQ(loaded[i].id, written[i].id) << "group " << i;
		EXPECT_EQ(loaded[i].speed, written[i].speed) << "group " << i;
		EXPECT_EQ(loaded[i].dirty, written[i].dirty) << "group " << i;
		EXPECT_EQ(loaded[i].members, written[i].members) << "group " << i << ", members in order";
	}

	std::vector<AoCSavedGroup> none;
	UnsignedInt zero = 0;
	{
		XferSave save;
		save.open(path.c_str());
		AoCSaveFidelity::xferGroups(&save, zero, none);
		save.close();
	}
	EXPECT_EQ(readFile(path).size(), 2 * sizeof(UnsignedInt));
	remove(path.c_str());
}

// The pathfind queue block (Pathfinder::xfer saves only a version, Pathfinder::crc covers the queue): every slot in order,
// then head, tail, the cells examined, the pending zone recalculation frame and the packed zones, back unchanged.
TEST(AoCSaveFidelityTest, PathfindQueueRoundTripsThroughItsSaveBlock)
{
	const std::string path = ::testing::TempDir() + "AoCSaveFidelityQueue.sav";
	AoCPathfindQueue written;
	written.requests.assign(512, INVALID_ID);
	written.requests[114] = (ObjectID)57;
	written.requests[115] = (ObjectID)23;
	written.head = 114;
	written.tail = 116;
	written.cells = 204;
	written.zoneFrame = 1817;
	const unsigned char packed[] = { 1, 0, 0, 0, 7, 9, 255 };
	written.zones.assign(packed, packed + sizeof(packed));
	{
		XferSave save;
		save.open(path.c_str());
		AoCSaveFidelity::xferPathfindQueue(&save, written);
		save.close();
	}
	EXPECT_EQ(readFile(path).size(), sizeof(UnsignedInt) + 512 * sizeof(ObjectID) + 3 * sizeof(Int) + sizeof(UnsignedInt) + sizeof(UnsignedInt) + sizeof(packed));
	AoCPathfindQueue loaded;
	{
		XferLoad load;
		load.open(path.c_str());
		AoCSaveFidelity::xferPathfindQueue(&load, loaded);
		load.close();
	}
	remove(path.c_str());
	EXPECT_EQ(loaded.requests, written.requests);
	EXPECT_EQ(loaded.head, 114);
	EXPECT_EQ(loaded.tail, 116);
	EXPECT_EQ(loaded.cells, 204);
	EXPECT_EQ(loaded.zoneFrame, 1817u);
	EXPECT_EQ(loaded.zones, written.zones) << "the packed zones as one blob";
	AoCPathfindQueue fresh;
	EXPECT_EQ(fresh.zoneFrame, 0xFFFFFFFFu) << "no recalculation pending by default";
}

// Decision 2: matchSlots finds each saved slot's module by key whatever order the live modules come in, and refuses
// any list that is not the same set of keys, each once.
TEST(AoCSaveFidelityTest, MatchSlotsPairsEverySavedSlotWithItsModule)
{
	std::vector<AoCSleepySlot> saved;
	saved.push_back(AoCSleepySlot((ObjectID)7, 2));
	saved.push_back(AoCSleepySlot((ObjectID)3, 0));
	saved.push_back(AoCSleepySlot((ObjectID)7, 0));
	saved.push_back(AoCSleepySlot((ObjectID)12, 5));
	std::vector<AoCSleepySlot> live;	// object order, module order
	live.push_back(AoCSleepySlot((ObjectID)3, 0));
	live.push_back(AoCSleepySlot((ObjectID)7, 0));
	live.push_back(AoCSleepySlot((ObjectID)7, 2));
	live.push_back(AoCSleepySlot((ObjectID)12, 5));

	std::vector<Int> order;
	ASSERT_TRUE(AoCSaveFidelity::matchSlots(saved, live, order));
	ASSERT_EQ(order.size(), saved.size());
	for (size_t i = 0; i < saved.size(); ++i)
		EXPECT_TRUE(live[order[i]] == saved[i]) << "slot " << i;

	std::vector<AoCSleepySlot> shorter(live.begin(), live.end() - 1);
	EXPECT_FALSE(AoCSaveFidelity::matchSlots(saved, shorter, order)) << "a module fewer";
	EXPECT_TRUE(order.empty());

	std::vector<AoCSleepySlot> other = live;
	other[3] = AoCSleepySlot((ObjectID)12, 4);
	EXPECT_FALSE(AoCSaveFidelity::matchSlots(saved, other, order)) << "a module the save does not know";

	std::vector<AoCSleepySlot> twice = saved;
	twice[3] = twice[0];
	EXPECT_FALSE(AoCSaveFidelity::matchSlots(twice, live, order)) << "one slot saved twice";

	std::vector<AoCSleepySlot> liveTwice = live;
	liveTwice[3] = liveTwice[0];
	EXPECT_FALSE(AoCSaveFidelity::matchSlots(saved, liveTwice, order)) << "two live modules with one key";

	std::vector<AoCSleepySlot> none;
	EXPECT_TRUE(AoCSaveFidelity::matchSlots(none, none, order)) << "an empty heap";
}

// Decision 2: the debug check of the restored heap: no entry before its parent, ties allowed.
TEST(AoCSaveFidelityTest, IsMinHeapAcceptsTiesAndRejectsAnInversion)
{
	std::vector<UnsignedInt> p;
	EXPECT_TRUE(AoCSaveFidelity::isMinHeap(p));
	p.push_back(40);
	EXPECT_TRUE(AoCSaveFidelity::isMinHeap(p));
	const UnsignedInt valid[] = { 8, 8, 12, 8, 9, 12, 20, 8 };
	p.assign(valid, valid + 8);
	EXPECT_TRUE(AoCSaveFidelity::isMinHeap(p));
	p[7] = 7;	// below its parent (index 3)
	EXPECT_FALSE(AoCSaveFidelity::isMinHeap(p));
	p[7] = 8;
	p[0] = 9;	// above its child at index 1
	EXPECT_FALSE(AoCSaveFidelity::isMinHeap(p));
}

// Decision 2 on a synthetic heap: modules due in one frame run in heap order, which a game's history shapes; the stock
// rebuild in object order (remake) runs those ties in another order, while the saved layout, written through the save
// block and put back slot by slot, runs them exactly as the saving game does, frame after frame.
TEST(AoCSaveFidelityTest, SavedLayoutKeepsTheOrderOfTiesWhereARebuildDoesNot)
{
	// History: twelve objects with one module each register over a few frames, sleep for various lengths, then all fall
	// due together at frame 40 (phase 2, PHASE_NORMAL).
	ModelHeap game;
	const UnsignedInt wake[12] = { 9, 3, 14, 3, 7, 22, 5, 9, 31, 2, 17, 6 };
	for (Int i = 0; i < 12; ++i)
	{
		Entry e;
		e.key = AoCSleepySlot((ObjectID)(100 + i), 1);
		e.priority = (wake[i] << 2) | 2;
		game.push(e);
	}
	for (UnsignedInt frame = 1; frame < 40; ++frame)
	{
		while (!game.a.empty() && (game.a[0].priority >> 2) <= frame)
		{
			Entry e = game.pop();
			e.priority = (40u << 2) | 2;
			game.push(e);
		}
	}
	ASSERT_TRUE(AoCSaveFidelity::isMinHeap(game.priorities()));

	// The save: the layout through its block.
	const std::string path = ::testing::TempDir() + "AoCSaveFidelityLayout.sav";
	{
		std::vector<AoCSleepySlot> slots = game.keys();
		XferSave save;
		save.open(path.c_str());
		AoCSaveFidelity::xferSlots(&save, slots);
		save.close();
		EXPECT_EQ(readFile(path).size(), sizeof(UnsignedInt) + 12 * (sizeof(ObjectID) + sizeof(UnsignedShort)));
	}
	std::vector<AoCSleepySlot> loadedSlots;
	{
		XferLoad load;
		load.open(path.c_str());
		AoCSaveFidelity::xferSlots(&load, loadedSlots);
		load.close();
	}
	remove(path.c_str());
	ASSERT_EQ(loadedSlots, game.keys());

	// The load: the modules come back in object order (GameLogic::loadPostProcess's collection pass).
	ModelHeap collected;
	for (Int i = 0; i < 12; ++i)
	{
		Entry e;
		e.key = AoCSleepySlot((ObjectID)(100 + i), 1);
		e.priority = (40u << 2) | 2;
		collected.a.push_back(e);
	}
	ModelHeap rebuilt = collected;
	rebuilt.remake();

	std::vector<AoCSleepySlot> live = collected.keys();
	std::vector<Int> order;
	ASSERT_TRUE(AoCSaveFidelity::matchSlots(loadedSlots, live, order));
	ModelHeap restored;
	for (size_t i = 0; i < order.size(); ++i)
		restored.a.push_back(collected.a[order[i]]);
	ASSERT_TRUE(AoCSaveFidelity::isMinHeap(restored.priorities()));
	EXPECT_EQ(restored.keys(), game.keys()) << "every module in its saved slot";

	// Frames 40, 50, 60: the restored heap runs the ties as the saving game does; the rebuild does not.
	Bool rebuiltDiffers = FALSE;
	for (UnsignedInt frame = 40; frame <= 60; frame += 10)
	{
		const std::vector<Int> expected = runFrame(game, frame, 10);
		const std::vector<Int> got = runFrame(restored, frame, 10);
		const std::vector<Int> stock = runFrame(rebuilt, frame, 10);
		ASSERT_EQ(expected.size(), 12u);
		EXPECT_EQ(got, expected) << "frame " << frame;
		if (stock != expected)
			rebuiltDiffers = TRUE;
	}
	EXPECT_TRUE(rebuiltDiffers) << "the stock rebuild runs ties in another order (the effect decision 2 removes)";
}
