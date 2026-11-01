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

// AoCSaveFidelity.cpp
// Age of Commanders F14a: the logic random generator and the sleepy-update heap layout in the save game (design D11
// sections 1.3 and 3.1). See AoCSaveFidelity.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCSaveFidelity.h"
#include "AoC/AoCHarness.h"
#include "Common/crc.h"
#include "Common/NameKeyGenerator.h"
#include "Common/RandomValue.h"
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/UpdateModule.h"

#include <algorithm>
#include <map>
#include <utility>
#include <stdarg.h>
#include <stdio.h>

// ------------------------------------------------------------------------------------------------
// The generator state
// ------------------------------------------------------------------------------------------------
AoCLogicRandomState::AoCLogicRandomState()
{
	for (Int i = 0; i < AOC_LOGIC_RANDOM_WORDS; ++i)
		seed[i] = 0;
	baseSeed = 0;
}

AoCLogicRandomState AoCLogicRandomState::live()
{
	AoCLogicRandomState s;
	AoCGetGameLogicRandomState(s.seed, &s.baseSeed);
	return s;
}

void AoCLogicRandomState::apply() const
{
	AoCSetGameLogicRandomState(seed, baseSeed);
}

UnsignedInt AoCLogicRandomState::seedCRC() const
{
	// Same computation as GetGameLogicRandomSeedCRC over the six words.
	CRC c;
	c.computeCRC(seed, sizeof(seed));
	return c.get();
}

Bool AoCLogicRandomState::operator==( const AoCLogicRandomState &other ) const
{
	for (Int i = 0; i < AOC_LOGIC_RANDOM_WORDS; ++i)
		if (seed[i] != other.seed[i])
			return FALSE;
	return baseSeed == other.baseSeed;
}

void AoCLogicRandomState::xfer( Xfer *xfer )
{
	for (Int i = 0; i < AOC_LOGIC_RANDOM_WORDS; ++i)
		xfer->xferUnsignedInt( &seed[i] );
	xfer->xferUnsignedInt( &baseSeed );
}

// ------------------------------------------------------------------------------------------------
// Pure parts
// ------------------------------------------------------------------------------------------------
namespace
{
	struct KeyedIndex
	{
		AoCSleepySlot key;
		Int index;
		bool operator<( const KeyedIndex &o ) const { return key < o.key; }
	};
}

Bool AoCSaveFidelity::matchSlots( const std::vector<AoCSleepySlot> &saved, const std::vector<AoCSleepySlot> &live, std::vector<Int> &order )
{
	order.clear();
	if (saved.size() != live.size())
		return FALSE;

	std::vector<KeyedIndex> sorted(live.size());
	for (size_t i = 0; i < live.size(); ++i)
	{
		sorted[i].key = live[i];
		sorted[i].index = (Int)i;
	}
	std::sort(sorted.begin(), sorted.end());
	for (size_t i = 1; i < sorted.size(); ++i)
		if (sorted[i].key == sorted[i - 1].key)
			return FALSE;	// two live modules with one key: the key does not identify a module

	std::vector<Bool> used(live.size(), FALSE);
	order.reserve(saved.size());
	for (size_t i = 0; i < saved.size(); ++i)
	{
		KeyedIndex probe;
		probe.key = saved[i];
		probe.index = 0;
		std::vector<KeyedIndex>::const_iterator it = std::lower_bound(sorted.begin(), sorted.end(), probe);
		if (it == sorted.end() || !(it->key == saved[i]) || used[it->index])
		{
			order.clear();
			return FALSE;	// a saved slot with no module, or a slot saved twice
		}
		used[it->index] = TRUE;
		order.push_back(it->index);
	}
	return TRUE;
}

Bool AoCSaveFidelity::isMinHeap( const std::vector<UnsignedInt> &priorities )
{
	for (size_t i = 1; i < priorities.size(); ++i)
		if (priorities[i] < priorities[(i - 1) / 2])
			return FALSE;
	return TRUE;
}

AoCLayoutPlan AoCSaveFidelity::planLayout( const std::vector<AoCSleepySlot> &saved, const std::vector<AoCSleepySlot> &live,
	const std::vector<UnsignedInt> &livePriorities, std::vector<Int> &order )
{
	if (livePriorities.size() != live.size() || !matchSlots(saved, live, order))
	{
		order.clear();
		return AOC_LAYOUT_NOT_MATCHED;
	}
	std::vector<UnsignedInt> priorities(order.size());
	for (size_t i = 0; i < order.size(); ++i)
		priorities[i] = livePriorities[order[i]];
	return isMinHeap(priorities) ? AOC_LAYOUT_RESTORED : AOC_LAYOUT_NOT_A_HEAP;
}

void AoCSaveFidelity::xferSlots( Xfer *xfer, AoCSleepyLayout &layout )
{
	UnsignedShort tagCount = (UnsignedShort)layout.tags.size();
	xfer->xferUnsignedShort( &tagCount );
	if (xfer->getXferMode() == XFER_LOAD)
		layout.tags.assign(tagCount, AsciiString::TheEmptyString);
	for (UnsignedShort t = 0; t < tagCount; ++t)
		xfer->xferAsciiString( &layout.tags[t] );

	UnsignedInt count = (UnsignedInt)layout.slots.size();
	xfer->xferUnsignedInt( &count );
	if (xfer->getXferMode() == XFER_LOAD)
	{
		layout.slots.clear();
		layout.slots.reserve(count);
		for (UnsignedInt i = 0; i < count; ++i)
		{
			ObjectID id = INVALID_ID;
			UnsignedShort tag = 0;
			xfer->xferObjectID( &id );
			xfer->xferUnsignedShort( &tag );
			layout.slots.push_back(AoCSleepySlot(id, tag));
		}
	}
	else
	{
		for (UnsignedInt i = 0; i < count; ++i)
		{
			ObjectID id = layout.slots[i].objectID;
			UnsignedShort tag = (UnsignedShort)layout.slots[i].moduleKey;
			xfer->xferObjectID( &id );
			xfer->xferUnsignedShort( &tag );
		}
	}
}

void AoCSaveFidelity::xferGroups( Xfer *xfer, UnsignedInt &lastGroupID, std::vector<AoCSavedGroup> &groups )
{
	xfer->xferUnsignedInt( &lastGroupID );
	UnsignedInt count = (UnsignedInt)groups.size();
	xfer->xferUnsignedInt( &count );
	if (xfer->getXferMode() == XFER_LOAD)
	{
		groups.clear();
		groups.resize(count);
	}
	for (UnsignedInt i = 0; i < count; ++i)
	{
		AoCSavedGroup &g = groups[i];
		xfer->xferUnsignedInt( &g.id );
		xfer->xferReal( &g.speed );
		xfer->xferBool( &g.dirty );
		UnsignedInt members = (UnsignedInt)g.members.size();
		xfer->xferUnsignedInt( &members );
		if (xfer->getXferMode() == XFER_LOAD)
			g.members.assign(members, INVALID_ID);
		for (UnsignedInt m = 0; m < members; ++m)
			xfer->xferObjectID( &g.members[m] );
	}
}

void AoCSaveFidelity::xferPathfindQueue( Xfer *xfer, AoCPathfindQueue &queue )
{
	UnsignedInt count = (UnsignedInt)queue.requests.size();
	xfer->xferUnsignedInt( &count );
	if (xfer->getXferMode() == XFER_LOAD)
		queue.requests.assign(count, INVALID_ID);
	for (UnsignedInt i = 0; i < count; ++i)
		xfer->xferObjectID( &queue.requests[i] );
	xfer->xferInt( &queue.head );
	xfer->xferInt( &queue.tail );
	xfer->xferInt( &queue.cells );
	xfer->xferUnsignedInt( &queue.zoneFrame );
	UnsignedInt bytes = (UnsignedInt)queue.zones.size();
	xfer->xferUnsignedInt( &bytes );
	if (xfer->getXferMode() == XFER_LOAD)
		queue.zones.assign(bytes, 0);
	if (bytes > 0)
		xfer->xferUser( &queue.zones[0], (Int)bytes );
}

// ------------------------------------------------------------------------------------------------
// Engine glue
// ------------------------------------------------------------------------------------------------
namespace
{
	void harnessLine( const char *format, ... );

	struct Pending
	{
		Bool hasRandom;
		AoCLogicRandomState random;
		Bool hasLayout;
		AoCSleepyLayout layout;
		Bool hasGroups;
		UnsignedInt lastGroupID;
		std::vector<AoCSavedGroup> groups;
		std::vector< std::pair<ObjectID, AsciiString> > attackInfos;	///< any save version
		Bool hasQueue;
		AoCPathfindQueue queue;

		Pending() : hasRandom( FALSE ), hasLayout( FALSE ), hasGroups( FALSE ), lastGroupID( 0 ), hasQueue( FALSE ) {}
	};

	/// Name each deferred attack priority set again, now that the script engine has loaded its sets.
	void restoreAttackInfos( Pending &p )
	{
		UnsignedInt restored = 0;
		UnsignedInt unknown = 0;
		for (size_t i = 0; i < p.attackInfos.size(); ++i)
		{
			Object *obj = TheGameLogic->findObjectByID(p.attackInfos[i].first);
			AIUpdateInterface *ai = obj ? obj->getAIUpdateInterface() : nullptr;
			if (ai == nullptr || TheScriptEngine == nullptr)
			{
				++unknown;
				continue;
			}
			const AttackPriorityInfo *info = TheScriptEngine->getAttackInfo(p.attackInfos[i].second);
			ai->setAttackInfo(info);
			if (info && info->getName() == p.attackInfos[i].second)
				++restored;
			else
				++unknown;
		}
		if (!p.attackInfos.empty())
			harnessLine("attack priority sets named again after the script engine loaded: %u units (%u not found)", restored, unknown);
		p.attackInfos.clear();
	}

	/// The AI groups as they are now, for the save: list order, raw speed and dirty bit.
	void liveGroups( UnsignedInt &lastGroupID, std::vector<AoCSavedGroup> &out )
	{
		out.clear();
		lastGroupID = TheAI ? TheAI->getAoCLastGroupID() : 0;
		if (TheAI == nullptr)
			return;
		const std::list<AIGroup *> &list = TheAI->getAoCGroupList();
		for (std::list<AIGroup *>::const_iterator it = list.begin(); it != list.end(); ++it)
		{
			AIGroup *group = *it;
			if (group == nullptr)
				continue;
			AoCSavedGroup g;
			g.id = group->getID();
			g.speed = group->getAoCRawSpeed();
			g.dirty = group->getAoCRawDirty();
			const ListObjectPtr &members = group->getAoCMemberList();
			for (ListObjectPtr::const_iterator m = members.begin(); m != members.end(); ++m)
				g.members.push_back(*m ? (*m)->getID() : INVALID_ID);
			out.push_back(g);
		}
	}

	/// Recreate the saved AI groups at the end of the list, with their ids, members, speed and dirty bit, then the id
	/// counter. A group with no member left alive would be destroyed as soon as it is made (members hold the references),
	/// so it is skipped and counted.
	void restoreGroups( Pending &p )
	{
		if (TheAI == nullptr)
			return;
		const UnsignedInt before = (UnsignedInt)TheAI->getAoCGroupList().size();
		UnsignedInt restored = 0;
		UnsignedInt skipped = 0;
		UnsignedInt missing = 0;
		for (size_t i = 0; i < p.groups.size(); ++i)
		{
			const AoCSavedGroup &g = p.groups[i];
			AIGroupPtr group = TheAI->createGroup();
			for (size_t m = 0; m < g.members.size(); ++m)
			{
				Object *obj = TheGameLogic->findObjectByID(g.members[m]);
				if (obj)
					group->add(obj);
				else
					++missing;
			}
			if (group->getCount() == 0)
			{
				++skipped;
#if RETAIL_COMPATIBLE_AIGROUP
				TheAI->destroyGroup(group);
#endif
				continue;	// released below by the smart pointer
			}
			group->setAoCSavedState(g.id, g.speed, g.dirty);
			++restored;
		}
		TheAI->setAoCLastGroupID(p.lastGroupID);
		harnessLine("AI groups restored: %u of %u (%u without members skipped, %u members not found, %u groups made during the load), last group id %u",
			restored, (UnsignedInt)p.groups.size(), skipped, missing, before, p.lastGroupID);
	}

	Pending &pending()
	{
		static Pending p;
		return p;
	}

	/// The heap key of one update module: its object's id and its module tag's name key, the key stock Object::xfer
	/// finds module data by, so a template whose module list changed order still matches.
	AoCSleepySlot slotOf( UpdateModule *u )
	{
		const Object *obj = u->friend_getObject();
		if (obj == nullptr)
			return AoCSleepySlot(INVALID_ID, AOC_SLEEPY_SLOT_NO_MODULE);
		return AoCSleepySlot(obj->getID(), (UnsignedInt)u->getModuleTagNameKey());
	}

	/// The heap's keyed slots as a save block's layout: each distinct tag once, by name, in first-use order.
	void packLayout( const std::vector<AoCSleepySlot> &keyed, AoCSleepyLayout &layout )
	{
		layout.tags.clear();
		layout.slots.clear();
		layout.slots.reserve(keyed.size());
		std::map<UnsignedInt, UnsignedShort> tagIndex;
		for (size_t i = 0; i < keyed.size(); ++i)
		{
			std::map<UnsignedInt, UnsignedShort>::const_iterator it = tagIndex.find(keyed[i].moduleKey);
			if (it == tagIndex.end())
			{
				const UnsignedShort index = (UnsignedShort)layout.tags.size();
				it = tagIndex.insert(std::make_pair(keyed[i].moduleKey, index)).first;
				layout.tags.push_back(keyed[i].moduleKey == AOC_SLEEPY_SLOT_NO_MODULE
					? AsciiString::TheEmptyString : TheNameKeyGenerator->keyToName((NameKeyType)keyed[i].moduleKey));
			}
			layout.slots.push_back(AoCSleepySlot(keyed[i].objectID, it->second));
		}
	}

	/// A loaded layout's slots keyed as slotOf keys the live modules (a tag index out of range or an empty name keys to
	/// AOC_SLEEPY_SLOT_NO_MODULE, which matches no module).
	void unpackLayout( const AoCSleepyLayout &layout, std::vector<AoCSleepySlot> &keyed )
	{
		std::vector<UnsignedInt> keys(layout.tags.size(), AOC_SLEEPY_SLOT_NO_MODULE);
		for (size_t t = 0; t < layout.tags.size(); ++t)
			if (!layout.tags[t].isEmpty())
				keys[t] = (UnsignedInt)TheNameKeyGenerator->nameToKey(layout.tags[t]);
		keyed.clear();
		keyed.reserve(layout.slots.size());
		for (size_t i = 0; i < layout.slots.size(); ++i)
		{
			const UnsignedInt tag = layout.slots[i].moduleKey;
			keyed.push_back(AoCSleepySlot(layout.slots[i].objectID, tag < keys.size() ? keys[tag] : (UnsignedInt)AOC_SLEEPY_SLOT_NO_MODULE));
		}
	}

	void harnessLine( const char *format, ... )
	{
		if (!AoCHarness::isActive())
			return;
		va_list args;
		va_start(args, format);
		fprintf(stderr, "[AoC save] frame %u ", TheGameLogic ? TheGameLogic->getFrame() : 0u);
		vfprintf(stderr, format, args);
		fprintf(stderr, "\n");
		fflush(stderr);
		va_end(args);
	}
}

void AoCSaveFidelity::xfer( Xfer *xfer, std::vector<UpdateModule *> &heap )
{
	Pending &p = pending();
	if (xfer->getXferMode() == XFER_LOAD)
	{
		p.random.xfer(xfer);
		p.hasRandom = TRUE;
		xferSlots(xfer, p.layout);
		p.hasLayout = TRUE;
		xferGroups(xfer, p.lastGroupID, p.groups);
		p.hasGroups = TRUE;
		xferPathfindQueue(xfer, p.queue);
		p.hasQueue = TRUE;
		return;
	}

	// Save (or a deep CRC in save mode): read only.
	AoCLogicRandomState random = AoCLogicRandomState::live();
	random.xfer(xfer);
	std::vector<AoCSleepySlot> keyed;
	keyed.reserve(heap.size());
	for (size_t i = 0; i < heap.size(); ++i)
		keyed.push_back(slotOf(heap[i]));
	AoCSleepyLayout layout;
	packLayout(keyed, layout);
	xferSlots(xfer, layout);
	UnsignedInt lastGroupID = 0;
	std::vector<AoCSavedGroup> groups;
	liveGroups(lastGroupID, groups);
	xferGroups(xfer, lastGroupID, groups);
	AoCPathfindQueue queue;
	if (TheAI && TheAI->pathfinder())
	{
		queue.requests.assign(PATHFIND_QUEUE_LEN, INVALID_ID);
		TheAI->pathfinder()->getAoCQueueState(&queue.requests[0], PATHFIND_QUEUE_LEN, &queue.head, &queue.tail, &queue.cells, &queue.zoneFrame);
		TheAI->pathfinder()->getAoCZoneData(queue.zones);
	}
	xferPathfindQueue(xfer, queue);
	if (xfer->getXferMode() == XFER_SAVE)
		harnessLine("wrote the logic random state (seed CRC %08X), the heap layout (%u modules, %u tags), %u AI groups (last group id %u) and the pathfind queue (head %d tail %d)",
			random.seedCRC(), (UnsignedInt)layout.slots.size(), (UnsignedInt)layout.tags.size(), (UnsignedInt)groups.size(), lastGroupID, queue.head, queue.tail);
}

void AoCSaveFidelity::beginObjectLoad()
{
	pending().attackInfos.clear();
}

void AoCSaveFidelity::deferAttackInfo( ObjectID id, const AsciiString &name )
{
	pending().attackInfos.push_back(std::make_pair(id, name));
}

void AoCSaveFidelity::clearPending()
{
	Pending &p = pending();
	p.hasRandom = FALSE;
	p.hasLayout = FALSE;
	p.layout = AoCSleepyLayout();
	p.hasGroups = FALSE;
	p.groups.clear();
	p.hasQueue = FALSE;
}

void AoCSaveFidelity::beginLoad()
{
	clearPending();
	Pending &p = pending();
	p.queue = AoCPathfindQueue();
	p.attackInfos.clear();
}

Bool AoCSaveFidelity::restoreSleepyLayout( std::vector<UpdateModule *> &heap )
{
	Pending &p = pending();
	if (!p.hasLayout)
		return FALSE;
	p.hasLayout = FALSE;

	std::vector<AoCSleepySlot> saved;
	unpackLayout(p.layout, saved);
	p.layout = AoCSleepyLayout();
	std::vector<AoCSleepySlot> live;
	std::vector<UnsignedInt> priorities;
	live.reserve(heap.size());
	priorities.reserve(heap.size());
	for (size_t i = 0; i < heap.size(); ++i)
	{
		live.push_back(slotOf(heap[i]));
		priorities.push_back(heap[i]->friend_getPriority());
	}

	std::vector<Int> order;
	const AoCLayoutPlan plan = planLayout(saved, live, priorities, order);
	if (plan == AOC_LAYOUT_NOT_MATCHED)
	{
		harnessLine("heap layout not restored: %u saved slots do not match the %u loaded modules; rebuilt in object order", (UnsignedInt)saved.size(), (UnsignedInt)heap.size());
		return FALSE;
	}

	std::vector<UpdateModule *> arranged(heap.size());
	for (size_t i = 0; i < order.size(); ++i)
		arranged[i] = heap[order[i]];
	heap.swap(arranged);
	for (size_t i = 0; i < heap.size(); ++i)
		heap[i]->friend_setIndexInLogic((Int)i);

	if (plan == AOC_LAYOUT_NOT_A_HEAP)
	{
		harnessLine("heap layout restored but not a heap (%u modules); heapified from the saved order", (UnsignedInt)heap.size());
		return FALSE;
	}
	harnessLine("heap layout restored: %u modules in their saved slots", (UnsignedInt)heap.size());
	return TRUE;
}

void AoCSaveFidelity::applyPendingLoad()
{
	Pending &p = pending();
	restoreAttackInfos(p);
	if (p.hasQueue)
	{
		p.hasQueue = FALSE;
		if (TheAI && TheAI->pathfinder() && !p.queue.requests.empty())
		{
			TheAI->pathfinder()->setAoCQueueState(&p.queue.requests[0], (Int)p.queue.requests.size(), p.queue.head, p.queue.tail, p.queue.cells, p.queue.zoneFrame);
			const Bool zones = !p.queue.zones.empty() && TheAI->pathfinder()->setAoCZoneData(p.queue.zones);
			harnessLine("pathfind queue restored (head %d tail %d cells %d, zones recalculated at frame %u); zones %s (%u bytes)", p.queue.head, p.queue.tail, p.queue.cells, p.queue.zoneFrame,
				zones ? "restored" : "not restored, computed fresh", (UnsignedInt)p.queue.zones.size());
		}
	}
	if (p.hasGroups)
	{
		p.hasGroups = FALSE;
		restoreGroups(p);
		p.groups.clear();
	}
	if (!p.hasRandom)
		return;
	p.hasRandom = FALSE;
	p.random.apply();
	harnessLine("logic random state restored (seed CRC %08X, base seed %u)", p.random.seedCRC(), p.random.baseSeed);
}
