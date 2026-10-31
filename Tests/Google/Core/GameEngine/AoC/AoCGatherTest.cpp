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

// AoC F2: the pure parts of typed gathering (design D11 section 2.2): the per-type capacity and delay lookup with
// the stock fall-backs, the INI parsers of the new module fields, the cargo type-switch rule, the node search's
// type rule, the gather rate multiplication point, and the save round trip and CRC rule of the runtime fields.

#include <stdio.h>
#include <string.h>
#include <string>
#include <gtest/gtest.h>

#include "AoC/AoCCost.h"
#include "AoC/AoCGather.h"
#include "AoC/AoCRules.h"
#include "Common/GameCommon.h"
#include "Common/INI.h"
#include "Common/Snapshot.h"
#include "Common/XferCRC.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "WWMath/wwmath.h"

namespace
{

enum { CASH = AOC_RESOURCE_CASH, FOOD = 1, WOOD = 2, IRON = 3, STEEL = 4 };

// The runtime fields as a module saves them: the module's own xfer calls AoCCargoState::xfer inside its versioned
// block, and its crc calls AoCCargoState::crc.
class CargoStateHolder : public Snapshot
{
public:
	AoCCargoState state;
protected:
	virtual void crc( Xfer *xfer ) override { state.crc(xfer); }
	virtual void xfer( Xfer *xfer ) override { state.xfer(xfer); }
	virtual void loadPostProcess() override {}
};

class AoCGatherTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_rules = new AoCRules;
		m_rules->init();
		TheAoCRules = m_rules;
	}

	void TearDown() override
	{
		TheAoCRules = nullptr;
		delete m_rules;
	}

	void defineStandardTypes()
	{
		m_rules->defineType("Food", "", "", 1000, FALSE, -1);
		m_rules->defineType("Wood", "", "", 1000, FALSE, -1);
		m_rules->defineType("Iron", "", "", 1500, FALSE, -1);
		m_rules->defineType("Steel", "", "", 4000, FALSE, -1);
	}

	// Drives a FieldParse procedure the way the INI loader does: the key token is consumed, the rest of the line
	// is read with the static INI tokenizer.
	static void startLine(char *buffer, size_t size, const char *line)
	{
		strncpy(buffer, line, size - 1);
		buffer[size - 1] = '\0';
		::strtok(buffer, INI::getSeps());
	}

	static AoCCost cost(const char *text, UnsignedInt *mask)
	{
		AoCCost c;
		AoCCost::parseText(text, c, mask);
		return c;
	}

	static UnsignedInt crcOf(CargoStateHolder &holder)
	{
		XferCRC xfer;
		xfer.open("lightCRC");
		xfer.xferSnapshot(&holder);
		UnsignedInt value = xfer.getCRC();
		xfer.close();
		return value;
	}

	AoCRules *m_rules;
};

TEST_F(AoCGatherTest, DefaultsAreTheStockGatherer)
{
	AoCCargoData data;
	EXPECT_TRUE(data.canCarry(CASH)) << "a gatherer carries cash unless AoCCargoTypes says otherwise";
	EXPECT_FALSE(data.canCarry(WOOD));
	EXPECT_FALSE(data.canCarry(-1));
	EXPECT_FALSE(data.canCarry(AOC_MAX_RESOURCES));
	EXPECT_EQ(data.maxUnitsFor(CASH, 4), 4) << "MaxBoxes when AoCMaxUnits does not name the type";
	EXPECT_EQ(data.maxUnitsFor(WOOD, 8), 8);
	EXPECT_EQ(data.gatherDelayFor(CASH, 30u), 30u) << "SupplyWarehouseActionDelay when AoCGatherDelay does not name the type";
	EXPECT_EQ(data.gatherDelayFor(-1, 30u), 30u);

	AoCCargoState state;
	EXPECT_EQ(state.cargoType, CASH);
	EXPECT_EQ(state.assignedType, -1);
}

TEST_F(AoCGatherTest, PerTypeCapacityAndDelayLookup)
{
	defineStandardTypes();
	AoCCargoData data;
	UnsignedInt mask = 0;
	AoCCost units = cost("Wood:10 Iron:5", &mask);
	data.setMaxUnits(units, mask);
	EXPECT_EQ(data.maxUnitsFor(WOOD, 1), 10);
	EXPECT_EQ(data.maxUnitsFor(IRON, 1), 5);
	EXPECT_EQ(data.maxUnitsFor(FOOD, 1), 1) << "a type not named falls back to MaxBoxes";
	EXPECT_EQ(data.maxUnitsFor(CASH, 4), 4);

	AoCCost zero = cost("Wood:0", &mask);
	data.setMaxUnits(zero, mask);
	EXPECT_EQ(data.maxUnitsFor(WOOD, 7), 0) << "an explicit 0 is a capacity of 0, not the fall-back";
	EXPECT_EQ(data.maxUnitsFor(IRON, 7), 7) << "a second AoCMaxUnits line replaces the first";

	AoCCost msecs = cost("Wood:3000 Iron:5000", &mask);
	data.setGatherDelayMsecs(msecs, mask);
	EXPECT_EQ(data.gatherDelayFor(WOOD, 9u), (UnsignedInt)(3 * LOGICFRAMES_PER_SECOND)) << "3000 ms at the live rate";
	EXPECT_EQ(data.gatherDelayFor(IRON, 9u), (UnsignedInt)(5 * LOGICFRAMES_PER_SECOND));
	EXPECT_EQ(data.gatherDelayFor(FOOD, 9u), 9u);
	EXPECT_EQ(data.gatherDelayFor(CASH, 9u), 9u);
}

TEST_F(AoCGatherTest, DelaysUseTheIniDurationRule)
{
	// INI::parseDurationUnsignedInt: ceiling of ms * LOGICFRAMES_PER_MSEC_REAL, so a delay is never shorter than asked.
	EXPECT_EQ(AoCGather::msecsToFrames(0), 0u);
	EXPECT_EQ(AoCGather::msecsToFrames(1), 1u) << "rounds up";
	EXPECT_EQ(AoCGather::msecsToFrames(1000), (UnsignedInt)LOGICFRAMES_PER_SECOND);
	EXPECT_EQ(AoCGather::msecsToFrames(150), (UnsignedInt)WWMath::Ceil(ConvertDurationFromMsecsToFrames(150.0f)));
	EXPECT_EQ(AoCGather::msecsToFrames(1200), (UnsignedInt)WWMath::Ceil(ConvertDurationFromMsecsToFrames(1200.0f)));
}

TEST_F(AoCGatherTest, ModuleFieldParsersReadAnIniLine)
{
	defineStandardTypes();
	char buffer[256];
	AoCCargoData data;

	startLine(buffer, sizeof(buffer), "AoCCargoTypes = Wood Cash");
	AoCCargoData::parseCargoTypes(nullptr, nullptr, &data, nullptr);
	EXPECT_TRUE(data.canCarry(WOOD));
	EXPECT_TRUE(data.canCarry(CASH));
	EXPECT_FALSE(data.canCarry(IRON));

	startLine(buffer, sizeof(buffer), "AoCMaxUnits = Wood:10");
	AoCCargoData::parseMaxUnits(nullptr, nullptr, &data, nullptr);
	EXPECT_EQ(data.maxUnitsFor(WOOD, 1), 10);

	startLine(buffer, sizeof(buffer), "AoCGatherDelay = Wood:3000");
	AoCCargoData::parseGatherDelay(nullptr, nullptr, &data, nullptr);
	EXPECT_EQ(data.gatherDelayFor(WOOD, 4u), AoCGather::msecsToFrames(3000));

	Int nodeType = CASH;
	startLine(buffer, sizeof(buffer), "AoCResourceType = Iron");
	AoCGather::parseResourceType(nullptr, nullptr, &nodeType, nullptr);
	EXPECT_EQ(nodeType, IRON);

	UnsignedInt accepts = 0;
	startLine(buffer, sizeof(buffer), "AoCAccepts = Food Wood Iron");
	AoCGather::parseResourceTypeList(nullptr, nullptr, &accepts, nullptr);
	EXPECT_EQ(accepts, AoCGather::typeBit(FOOD) | AoCGather::typeBit(WOOD) | AoCGather::typeBit(IRON));

	startLine(buffer, sizeof(buffer), "AoCAccepts = Gold");
	EXPECT_THROW(AoCGather::parseResourceTypeList(nullptr, nullptr, &accepts, nullptr), decltype(INI_INVALID_DATA)) << "unknown type";
	startLine(buffer, sizeof(buffer), "AoCAccepts =");
	EXPECT_THROW(AoCGather::parseResourceTypeList(nullptr, nullptr, &accepts, nullptr), decltype(INI_INVALID_DATA)) << "empty list";
	startLine(buffer, sizeof(buffer), "AoCResourceType = Gold");
	EXPECT_THROW(AoCGather::parseResourceType(nullptr, nullptr, &nodeType, nullptr), decltype(INI_INVALID_DATA));
}

TEST_F(AoCGatherTest, TypeNamesResolveThroughTheRules)
{
	defineStandardTypes();
	EXPECT_EQ(AoCGather::parseTypeName("wood"), WOOD) << "case-insensitive, like AoCCost";
	EXPECT_EQ(AoCGather::parseTypeListText("Wood Cash"), AoCGather::typeBit(WOOD) | AoCGather::typeBit(CASH));
	EXPECT_EQ(AoCGather::parseTypeListText(""), 0u);
	EXPECT_THROW(AoCGather::parseTypeListText("Wood Gold"), decltype(INI_INVALID_DATA));
	EXPECT_STREQ(AoCGather::typeName(IRON), "Iron");
	EXPECT_STREQ(AoCGather::typeName(42), "?");

	// Without types defined only cash exists, so the stock data's absent fields are the only valid ones.
	TheAoCRules = nullptr;
	EXPECT_EQ(AoCGather::parseTypeListText("Cash"), AoCGather::typeBit(CASH));
	EXPECT_THROW(AoCGather::parseTypeName("Wood"), decltype(INI_INVALID_DATA));
	EXPECT_STREQ(AoCGather::typeName(CASH), "Cash");
	TheAoCRules = m_rules;
}

TEST_F(AoCGatherTest, CargoTypeSwitchDropsTheLoadFirst)
{
	// D2 3.1: switching type discards the load; the same type, or an empty load, keeps going.
	EXPECT_TRUE(AoCGather::mustDropBeforePickup(WOOD, 4, IRON)) << "carrying wood at an iron node";
	EXPECT_TRUE(AoCGather::mustDropBeforePickup(CASH, 1, WOOD));
	EXPECT_FALSE(AoCGather::mustDropBeforePickup(WOOD, 4, WOOD)) << "same type: keep filling";
	EXPECT_FALSE(AoCGather::mustDropBeforePickup(WOOD, 0, IRON)) << "nothing to drop";
	EXPECT_FALSE(AoCGather::mustDropBeforePickup(CASH, 3, CASH)) << "the stock path";
}

TEST_F(AoCGatherTest, NodeSearchKeepsToTheAssignedType)
{
	const UnsignedInt woodAndCash = AoCGather::typeBit(WOOD) | AoCGather::typeBit(CASH);
	// unassigned: any type it may carry
	EXPECT_TRUE(AoCGather::wantsNodeType(WOOD, -1, woodAndCash));
	EXPECT_TRUE(AoCGather::wantsNodeType(CASH, -1, woodAndCash));
	EXPECT_FALSE(AoCGather::wantsNodeType(IRON, -1, woodAndCash));
	// assigned by the player: that type only
	EXPECT_TRUE(AoCGather::wantsNodeType(WOOD, WOOD, woodAndCash));
	EXPECT_FALSE(AoCGather::wantsNodeType(CASH, WOOD, woodAndCash)) << "a wood worker does not wander to a supply dock";
	// stock: cash gatherer, cash node, never assigned
	EXPECT_TRUE(AoCGather::wantsNodeType(CASH, -1, AoCGather::typeBit(CASH)));
}

TEST_F(AoCGatherTest, GatherRateScalesTheDelay)
{
	EXPECT_EQ(AoCGather::scaleDelayByRate(90u, 1000), 90u) << "the identity until F13 adds the player's rate";
	EXPECT_EQ(AoCGather::scaleDelayByRate(90u, 1500), 60u) << "50 percent faster";
	EXPECT_EQ(AoCGather::scaleDelayByRate(100u, 1100), 90u) << "integer division, rounding down";
	EXPECT_EQ(AoCGather::scaleDelayByRate(90u, 500), 180u) << "a slower rate lengthens the delay";
	EXPECT_EQ(AoCGather::scaleDelayByRate(90u, 0), 90u) << "a rate of 0 is ignored rather than dividing by zero";
}

TEST_F(AoCGatherTest, RuntimeFieldsRoundTripAndMapByTypeName)
{
	defineStandardTypes();
	const std::string path = ::testing::TempDir() + "AoCGatherCargoRoundTrip.sav";

	{
		CargoStateHolder holder;
		holder.state.cargoType = WOOD;
		holder.state.assignedType = IRON;
		XferSave save;
		save.open(path.c_str());
		save.xferSnapshot(m_rules);	// as PlayerList::xfer writes TheAoCRules ahead of the objects
		save.xferSnapshot(&holder);
		save.close();
	}

	// Same type set: the values come back.
	{
		CargoStateHolder loaded;
		XferLoad load;
		load.setOptions(XO_NO_POST_PROCESSING);
		load.open(path.c_str());
		load.xferSnapshot(m_rules);
		load.xferSnapshot(&loaded);
		load.close();
		EXPECT_EQ(loaded.state.cargoType, WOOD);
		EXPECT_EQ(loaded.state.assignedType, IRON);
	}

	// Another type set (Iron first, no Wood): the indices map by name; a type that is gone becomes cash / none.
	{
		AoCRules other;
		other.init();
		other.defineType("Iron", "", "", 1500, FALSE, -1);
		other.defineType("Food", "", "", 1000, FALSE, -1);
		TheAoCRules = &other;
		CargoStateHolder loaded;
		XferLoad load;
		load.setOptions(XO_NO_POST_PROCESSING);
		load.open(path.c_str());
		load.xferSnapshot(&other);
		load.xferSnapshot(&loaded);
		load.close();
		EXPECT_EQ(loaded.state.cargoType, CASH) << "Wood is gone from this type set";
		EXPECT_EQ(loaded.state.assignedType, 1) << "Iron is index 1 here";
		TheAoCRules = m_rules;
	}

	remove(path.c_str());
}

TEST_F(AoCGatherTest, CrcIsSilentWhileDisabledAndCoversBothFieldsWhenEnabled)
{
	CargoStateHolder a, b;
	b.state.cargoType = 3;
	b.state.assignedType = 2;
	EXPECT_EQ(crcOf(a), crcOf(b)) << "stock data: the fields stay out of the CRC";

	defineStandardTypes();
	CargoStateHolder c, d, e;
	d.state.cargoType = WOOD;
	e.state.assignedType = WOOD;
	EXPECT_NE(crcOf(c), crcOf(d)) << "the cargo type is in the CRC";
	EXPECT_NE(crcOf(c), crcOf(e)) << "the assigned type is in the CRC";
	EXPECT_NE(crcOf(d), crcOf(e));
}

} // namespace
