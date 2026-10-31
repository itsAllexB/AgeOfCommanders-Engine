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

// AoC F4: the population counters (design D11 section 2.4): reserve at queue time and release at the door
// or on cancel, headroom against housing and the lobby cap (0 = no lobby cap), going over the cap, the
// at-cap state whose edges reach the player, the script condition AOC_PLAYER_POPULATION_HEADROOM, the save
// round trip and the CRC rule. The population runs standalone here (no owning Player, no GameLogic), so
// the lobby cap is the detached value.

#include <stdio.h>
#include <string>
#include <gtest/gtest.h>

#include "AoC/AoCPopulation.h"
#include "Common/Debug.h"
#include "Common/Module.h"
#include "Common/XferCRC.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Scripts.h"

namespace
{

// A behaviour module whose object comes with units: what TransportContain, RiderChangeContain, OverlordContain and
// HelixContain module data report from their payload (review of PR #5, finding 1).
class FakePayloadModuleData : public ModuleData
{
public:
	explicit FakePayloadModuleData( Int population ) : m_population(population) { }
	virtual Int getAoCInitialPayloadPopulation() const override { return m_population; }
private:
	Int m_population;
};

class AoCPopulationTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		AoCPopulation::setDetachedLobbyCap(0);
		m_pop.init(nullptr);
	}

	void TearDown() override
	{
		AoCPopulation::setDetachedLobbyCap(0);
	}

	// The light CRC of one population, as Player::crc computes it through xferSnapshot.
	static UnsignedInt crcOf(AoCPopulation &pop)
	{
		XferCRC xfer;
		xfer.open("lightCRC");
		xfer.xferSnapshot(&pop);
		UnsignedInt value = xfer.getCRC();
		xfer.close();
		return value;
	}

	// A Player::xfer v10 tail in miniature: the population snapshot alone.
	static void saveTo(const std::string &path, AoCPopulation &pop)
	{
		XferSave save;
		save.open(path.c_str());
		save.xferSnapshot(&pop);
		save.close();
	}

	static void loadFrom(const std::string &path, AoCPopulation &pop)
	{
		XferLoad load;
		load.setOptions(XO_NO_POST_PROCESSING);	// no TheGameState to queue post-processing on
		load.open(path.c_str());
		load.xferSnapshot(&pop);
		load.close();
	}

	AoCPopulation m_pop;
};

// --- the stock case: nothing counts, nothing is refused --------------------------------------------

TEST_F(AoCPopulationTest, StartsInactiveAndRefusesNothingFree)
{
	EXPECT_EQ(m_pop.getUsed(), 0);
	EXPECT_EQ(m_pop.getCapacity(), 0);
	EXPECT_EQ(m_pop.getReserved(), 0);
	EXPECT_EQ(m_pop.getHeadroom(), 0);
	EXPECT_FALSE(m_pop.isActive()) << "stock data: every template at cost 0 and capacity 0";
	EXPECT_FALSE(m_pop.isAtCap()) << "an inactive player is never at the cap, so no warning in a stock game";
	EXPECT_TRUE(m_pop.canAfford(0)) << "a unit without population is never refused";
	m_pop.reserve(0);
	m_pop.release(0);
	m_pop.addUsed(0);
	m_pop.addCapacity(0);
	EXPECT_FALSE(m_pop.isActive());
}

// --- reserve, release, headroom --------------------------------------------------------------------

TEST_F(AoCPopulationTest, ReserveAtQueueReleaseAtTheDoorOrOnCancel)
{
	m_pop.addCapacity(10);	// a town centre
	EXPECT_EQ(m_pop.getHeadroom(), 10);

	// Queue a batch of two (a QuantityModifier of 2, cost 1 each): both are reserved at once.
	ASSERT_TRUE(m_pop.canAfford(2));
	m_pop.reserve(2);
	EXPECT_EQ(m_pop.getReserved(), 2);
	EXPECT_EQ(m_pop.getHeadroom(), 8);

	// The first unit leaves the door: reserved to used, the headroom does not move.
	m_pop.release(1);
	m_pop.addUsed(1);
	EXPECT_EQ(m_pop.getUsed(), 1);
	EXPECT_EQ(m_pop.getReserved(), 1);
	EXPECT_EQ(m_pop.getHeadroom(), 8);

	// Queue a tank (cost 3), then cancel it: the headroom comes back.
	m_pop.reserve(3);
	EXPECT_EQ(m_pop.getHeadroom(), 5);
	m_pop.release(3);
	EXPECT_EQ(m_pop.getHeadroom(), 8);

	// The second unit of the batch leaves the door.
	m_pop.release(1);
	m_pop.addUsed(1);
	EXPECT_EQ(m_pop.getUsed(), 2);
	EXPECT_EQ(m_pop.getReserved(), 0);
	EXPECT_EQ(m_pop.getHeadroom(), 8);
}

TEST_F(AoCPopulationTest, HeadroomCountsUsedAndReserved)
{
	m_pop.addCapacity(10);
	m_pop.addUsed(4);
	m_pop.reserve(2);
	EXPECT_EQ(m_pop.getHeadroom(), 4);
	EXPECT_TRUE(m_pop.canAfford(4));
	EXPECT_FALSE(m_pop.canAfford(5)) << "production refuses a unit the headroom does not cover";
	EXPECT_EQ(AoCPopulation::headroomFor(4, 10, 2, 0), 4);

	// A house completes, then is destroyed.
	m_pop.addCapacity(5);
	EXPECT_EQ(m_pop.getHeadroom(), 9);
	m_pop.addCapacity(-5);
	EXPECT_EQ(m_pop.getHeadroom(), 4);
}

// --- the lobby cap: 0 means no lobby cap, housing still limits --------------------------------------

TEST_F(AoCPopulationTest, LobbyCapZeroIsUnlimitedAndOtherwiseClampsCapacity)
{
	EXPECT_EQ(AoCPopulation::capFor(25, 0), 25) << "lobby cap 0: only housing limits";
	EXPECT_EQ(AoCPopulation::capFor(25, 12), 12) << "the lobby cap binds below the housing";
	EXPECT_EQ(AoCPopulation::capFor(8, 12), 8) << "housing binds below the lobby cap";
	EXPECT_EQ(AoCPopulation::capFor(25, -1), AOC_POPULATION_CAP_UNLIMITED) << "F7: -1 is the unlimited mode, not housing only";
	EXPECT_EQ(AoCPopulation::headroomFor(10, 25, 2, 12), 0);

	m_pop.addCapacity(20);
	m_pop.addUsed(5);
	EXPECT_EQ(m_pop.getCap(), 20);
	EXPECT_EQ(m_pop.getHeadroom(), 15);

	AoCPopulation::setDetachedLobbyCap(12);
	EXPECT_EQ(m_pop.getCap(), 12);
	EXPECT_EQ(m_pop.getHeadroom(), 7);
	EXPECT_TRUE(m_pop.canAfford(7));
	EXPECT_FALSE(m_pop.canAfford(8));
}

// --- F7: the unlimited lobby cap (-1) turns the housing check off; the counters still count ---------

TEST_F(AoCPopulationTest, UnlimitedCapTurnsTheCheckOffAndKeepsCounting)
{
	EXPECT_TRUE(AoCPopulation::isUnlimited(AOC_POPULATION_CAP_UNLIMITED));
	EXPECT_FALSE(AoCPopulation::isUnlimited(0)) << "0 is no lobby clamp: housing still limits";
	EXPECT_FALSE(AoCPopulation::isUnlimited(12));
	EXPECT_EQ(AoCPopulation::capFor(10, -1), AOC_POPULATION_CAP_UNLIMITED);
	EXPECT_EQ(AoCPopulation::capFor(0, -1), AOC_POPULATION_CAP_UNLIMITED) << "no housing at all and still no cap";
	EXPECT_EQ(AoCPopulation::headroomFor(500, 10, 40, -1), (Int)AOC_POPULATION_UNLIMITED_HEADROOM);

	AoCPopulation::setDetachedLobbyCap(AOC_POPULATION_CAP_UNLIMITED);
	m_pop.addCapacity(10);
	m_pop.addUsed(10);
	EXPECT_EQ(m_pop.getCap(), AOC_POPULATION_CAP_UNLIMITED);
	EXPECT_FALSE(m_pop.isAtCap()) << "full housing is not a cap any more";
	EXPECT_TRUE(m_pop.canAfford(1));
	EXPECT_TRUE(m_pop.canAfford(9)) << "a crawler with its payload fits";
	m_pop.reserve(9);
	m_pop.addUsed(25);
	EXPECT_EQ(m_pop.getUsed(), 35) << "the counters still count for display";
	EXPECT_EQ(m_pop.getCapacity(), 10);
	EXPECT_EQ(m_pop.getReserved(), 9);
	EXPECT_FALSE(m_pop.isAtCap());
	EXPECT_TRUE(m_pop.canAfford(1000));
	EXPECT_TRUE(m_pop.scriptHeadroomIs(Parameter::GREATER, 1000)) << "a script sees room to spare";

	// The same counters under a real cap again: over it, blocked (the cap is a game setting, but the arithmetic is shared).
	AoCPopulation::setDetachedLobbyCap(0);
	m_pop.reserve(0);
	EXPECT_EQ(m_pop.getCap(), 10);
	EXPECT_EQ(m_pop.getHeadroom(), 10 - 35 - 9);
	EXPECT_FALSE(m_pop.canAfford(1));
}

// --- going over the cap: allowed, nothing dies, production waits ------------------------------------

TEST_F(AoCPopulationTest, OverCapacityIsAllowedAndBlocksOnlyUnitsWithPopulation)
{
	m_pop.addCapacity(15);
	m_pop.addUsed(12);
	m_pop.addCapacity(-5);	// a house is destroyed
	EXPECT_EQ(m_pop.getHeadroom(), -2);
	EXPECT_EQ(m_pop.getUsed(), 12) << "nobody dies";
	EXPECT_TRUE(m_pop.isAtCap());
	EXPECT_FALSE(m_pop.canAfford(1));
	EXPECT_TRUE(m_pop.canAfford(0)) << "a unit without population still builds over the cap";

	m_pop.addUsed(3);	// a capture or a paradrop adds more
	EXPECT_EQ(m_pop.getHeadroom(), -5);

	m_pop.addUsed(-6);	// losses bring the player back under
	EXPECT_EQ(m_pop.getHeadroom(), 1);
	EXPECT_FALSE(m_pop.isAtCap());
	EXPECT_TRUE(m_pop.canAfford(1));
}

// --- the at-cap state (its edges reach Player::onAoCPopulationBlockChange) --------------------------

TEST_F(AoCPopulationTest, AtCapFollowsTheHeadroom)
{
	m_pop.addCapacity(2);
	EXPECT_FALSE(m_pop.isAtCap());
	m_pop.reserve(1);
	EXPECT_FALSE(m_pop.isAtCap());
	m_pop.reserve(1);
	EXPECT_TRUE(m_pop.isAtCap()) << "queueing the last slot reaches the cap";
	m_pop.release(1);
	EXPECT_FALSE(m_pop.isAtCap());
	m_pop.reserve(1);
	EXPECT_TRUE(m_pop.isAtCap());
	m_pop.addCapacity(5);
	EXPECT_FALSE(m_pop.isAtCap()) << "a completed house leaves the cap";

	m_pop.reset();
	EXPECT_FALSE(m_pop.isAtCap());
	EXPECT_FALSE(m_pop.isActive());
}

// --- the script condition AOC_PLAYER_POPULATION_HEADROOM (side, comparison, int) --------------------

TEST_F(AoCPopulationTest, ScriptConditionHeadroom)
{
	m_pop.addCapacity(10);
	m_pop.addUsed(7);	// headroom 3
	EXPECT_TRUE(m_pop.scriptHeadroomIs(Parameter::EQUAL, 3));
	EXPECT_TRUE(m_pop.scriptHeadroomIs(Parameter::GREATER, 2));
	EXPECT_FALSE(m_pop.scriptHeadroomIs(Parameter::GREATER, 3));
	EXPECT_TRUE(m_pop.scriptHeadroomIs(Parameter::GREATER_EQUAL, 3));
	EXPECT_TRUE(m_pop.scriptHeadroomIs(Parameter::LESS_THAN, 4));
	EXPECT_TRUE(m_pop.scriptHeadroomIs(Parameter::LESS_EQUAL, 3));
	EXPECT_TRUE(m_pop.scriptHeadroomIs(Parameter::NOT_EQUAL, 0));
	m_pop.addUsed(5);	// headroom -2
	EXPECT_TRUE(m_pop.scriptHeadroomIs(Parameter::LESS_THAN, 0));
}

// --- save and load -----------------------------------------------------------------------------------

TEST_F(AoCPopulationTest, XferRoundTrip)
{
	m_pop.addCapacity(20);
	m_pop.addUsed(17);
	m_pop.reserve(3);
	ASSERT_TRUE(m_pop.isAtCap());
	const std::string path = ::testing::TempDir() + "AoCPopulationXferRoundTrip.sav";
	saveTo(path, m_pop);

	AoCPopulation loaded;
	loaded.init(nullptr);
	loaded.addCapacity(99);	// whatever it held before the load is replaced
	loadFrom(path, loaded);
	EXPECT_EQ(loaded.getUsed(), 17);
	EXPECT_EQ(loaded.getCapacity(), 20);
	EXPECT_EQ(loaded.getReserved(), 3);
	EXPECT_EQ(loaded.getHeadroom(), 0);
	EXPECT_TRUE(loaded.isAtCap()) << "the derived at-cap state follows the loaded counters";

	// Player::loadPostProcess recounts used and capacity from the objects; reserved stays as saved.
	loaded.recount(15, 25);
	EXPECT_EQ(loaded.getUsed(), 15);
	EXPECT_EQ(loaded.getCapacity(), 25);
	EXPECT_EQ(loaded.getReserved(), 3);
	EXPECT_FALSE(loaded.isAtCap());
	remove(path.c_str());
}

// --- CRC rule: nothing enters the per-frame CRC while every counter is zero -------------------------

TEST_F(AoCPopulationTest, CrcIsSilentWhileInactiveAndCoversEveryCounter)
{
	AoCPopulation empty;
	empty.init(nullptr);
	XferCRC nothing;
	nothing.open("lightCRC");
	const UnsignedInt nothingCRC = nothing.getCRC();
	nothing.close();
	EXPECT_EQ(crcOf(empty), nothingCRC) << "stock data: the population adds nothing to the CRC";

	m_pop.addCapacity(10);
	const UnsignedInt capacityCRC = crcOf(m_pop);
	EXPECT_NE(capacityCRC, nothingCRC);
	m_pop.addUsed(1);
	const UnsignedInt usedCRC = crcOf(m_pop);
	EXPECT_NE(usedCRC, capacityCRC) << "used enters the CRC";
	m_pop.reserve(1);
	const UnsignedInt reservedCRC = crcOf(m_pop);
	EXPECT_NE(reservedCRC, usedCRC) << "reserved enters the CRC";
	AoCPopulation::setDetachedLobbyCap(12);
	EXPECT_NE(crcOf(m_pop), reservedCRC) << "the lobby cap enters the CRC";
}

// --- the load recount (review of PR #5, nit 8) ---------------------------------------------------------
// Player::loadPostProcess -> recountFromOwner counts the owner's objects and applies applyRecount. The hook paths
// themselves (team joins, the construction bit, death) need Objects, and a Player needs TheNameKeyGenerator and the
// rank store in Player::init, so the game runs cover those; these tests cover the decision the recount applies.

TEST_F(AoCPopulationTest, RecountKeepsTheSavedReservation)
{
	m_pop.addCapacity(20);
	m_pop.addUsed(9);
	m_pop.reserve(3);
	const std::string path = ::testing::TempDir() + "AoCPopulationRecount.sav";
	saveTo(path, m_pop);

	AoCPopulation loaded;
	loaded.init(nullptr);
	loadFrom(path, loaded);
	loaded.applyRecount(9, 20, 5);	// the objects say 9 and 20; the queues would say 5
	EXPECT_EQ(loaded.getUsed(), 9);
	EXPECT_EQ(loaded.getCapacity(), 20);
	EXPECT_EQ(loaded.getReserved(), 3) << "a save with the population block keeps its reservation";

	loaded.applyRecount(9, 20, 5);	// a later recount, with no load in between
	EXPECT_EQ(loaded.getReserved(), 5) << "only the recount right after a load keeps the saved value";
	remove(path.c_str());
}

TEST_F(AoCPopulationTest, RecountWithoutTheBlockTakesTheQueues)
{
	// A save from before F4 (Player::xfer v9) has no population block: Player::xfer resets, nothing is loaded.
	m_pop.reset();
	m_pop.applyRecount(12, 10, 4);
	EXPECT_EQ(m_pop.getUsed(), 12);
	EXPECT_EQ(m_pop.getCapacity(), 10);
	EXPECT_EQ(m_pop.getReserved(), 4) << "reserved comes from the production queues";
	EXPECT_TRUE(m_pop.isAtCap()) << "set by the recount, without an edge report";
}

#if !defined(DEBUG_CRASHING)	// releasing past the reservation is a DEBUG_CRASH; release builds clamp
TEST_F(AoCPopulationTest, ReleasePastTheReservationStopsAtZero)
{
	m_pop.addCapacity(10);
	m_pop.reserve(2);
	m_pop.release(5);
	EXPECT_EQ(m_pop.getReserved(), 0) << "reserved never goes below zero";
	EXPECT_EQ(m_pop.getHeadroom(), 10);
}
#endif

// --- a produced transport's initial payload counts with it (review of PR #5, finding 1) ---------------

TEST_F(AoCPopulationTest, ProductionCostAddsTheInitialPayload)
{
	ModuleData otherModule;						// any module without a payload reports 0
	FakePayloadModuleData redGuards(8);	// a Troop Crawler's eight Red Guards at 1 each
	const ModuleData *crawler[] = { &otherModule, &redGuards, &otherModule };
	const ModuleData *tank[] = { &otherModule, &otherModule };

	EXPECT_EQ(AoCPopulation::productionCostOf(2, crawler, 3), 10) << "D3: a light vehicle at 2 plus its passengers";
	EXPECT_EQ(AoCPopulation::productionCostOf(0, crawler, 3), 8) << "a transport without a cost still needs room for its passengers";
	EXPECT_EQ(AoCPopulation::productionCostOf(3, tank, 2), 3) << "no payload: the template's own cost";
	EXPECT_EQ(AoCPopulation::productionCostOf(1, nullptr, 0), 1);
	EXPECT_EQ(AoCPopulation::productionCostOf(nullptr), 0);
	EXPECT_EQ(AoCPopulation::payloadPopulationOf(nullptr), 0);

	// The 60 Hz case of test e: one slot left, the crawler and its squad need 9 at test-overlay prices.
	m_pop.addCapacity(10);
	m_pop.addUsed(9);
	const Int crawlerNeeds = AoCPopulation::productionCostOf(1, crawler, 3);
	EXPECT_EQ(crawlerNeeds, 9);
	EXPECT_FALSE(m_pop.canAfford(crawlerNeeds)) << "the squad does not fit, so production refuses the crawler";
	EXPECT_TRUE(m_pop.canAfford(1)) << "a lone infantryman still fits";

	// With room: the whole squad is reserved at the queue and released in one go at the door, when the
	// passengers join with the transport.
	m_pop.addCapacity(10);	// headroom 11
	ASSERT_TRUE(m_pop.canAfford(crawlerNeeds));
	m_pop.reserve(crawlerNeeds);
	EXPECT_EQ(m_pop.getHeadroom(), 2);
	m_pop.addUsed(crawlerNeeds);		// the crawler and its eight passengers join
	m_pop.release(crawlerNeeds);		// the reservation leaves in the same frame
	EXPECT_EQ(m_pop.getUsed(), 18);
	EXPECT_EQ(m_pop.getReserved(), 0);
	EXPECT_EQ(m_pop.getHeadroom(), 2) << "the door moves the squad from reserved to used without changing the headroom";
}

} // namespace
