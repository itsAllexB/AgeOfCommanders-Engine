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
#include "Common/XferCRC.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Scripts.h"

namespace
{

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
	EXPECT_EQ(AoCPopulation::capFor(25, -1), 25) << "a negative cap is no cap";
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

} // namespace
