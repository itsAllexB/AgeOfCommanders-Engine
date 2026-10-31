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

// AoC F1: AoCCost parsing and arithmetic, and the AoCRules type table (design D11 section 2.1).

#include <string.h>
#include <gtest/gtest.h>

#include "AoC/AoCCost.h"
#include "AoC/AoCRules.h"
#include "Common/INI.h"
#include "GameLogic/Scripts.h"

namespace
{

// The D2 resource set: Cash is the implicit index 0; Food, Wood, Iron, Steel follow in definition order.
class AoCCostTest : public ::testing::Test
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
		m_rules->defineType("Food", "GUI:AoCFood", "AoCFoodIcon", 1000, FALSE, -1);
		m_rules->defineType("Wood", "GUI:AoCWood", "AoCWoodIcon", 1000, FALSE, -1);
		m_rules->defineType("Iron", "GUI:AoCIron", "AoCIronIcon", 1500, FALSE, -1);
		m_rules->defineType("Steel", "GUI:AoCSteel", "AoCSteelIcon", 4000, FALSE, -1);
	}

	// Drives AoCCost::parseFromINI the way the INI loader does: the key token is consumed, the rest of the
	// line is read with the static INI tokenizer.
	static void parseLine(const char *line, AoCCost &out, UnsignedInt *mask)
	{
		char buffer[256];
		strncpy(buffer, line, sizeof(buffer) - 1);
		buffer[sizeof(buffer) - 1] = '\0';
		::strtok(buffer, INI::getSeps());
		AoCCost::parseFromINI(nullptr, out, mask);
	}

	AoCRules *m_rules;
};

TEST_F(AoCCostTest, RulesStartDisabledWithOnlyCash)
{
	EXPECT_FALSE(m_rules->isEnabled());
	EXPECT_FALSE(AoCRules::enabled());
	EXPECT_EQ(m_rules->getTypeCount(), 1);
	EXPECT_EQ(m_rules->findType("Cash"), AOC_RESOURCE_CASH);
	EXPECT_EQ(m_rules->findType("cash"), AOC_RESOURCE_CASH);
	EXPECT_EQ(m_rules->findType("Wood"), -1);
	EXPECT_EQ(m_rules->getCashEquivalentPerMille(AOC_RESOURCE_CASH), 1000);
}

TEST_F(AoCCostTest, DefiningTypesEnablesTheFeature)
{
	defineStandardTypes();
	EXPECT_TRUE(m_rules->isEnabled());
	EXPECT_EQ(m_rules->getTypeCount(), 5);
	EXPECT_EQ(m_rules->findType("Food"), 1);
	EXPECT_EQ(m_rules->findType("WOOD"), 2);
	EXPECT_EQ(m_rules->findType("Iron"), 3);
	EXPECT_EQ(m_rules->findType("Steel"), 4);
	EXPECT_EQ(m_rules->getCashEquivalentPerMille(4), 4000);
	EXPECT_STREQ(m_rules->getDisplayLabel(2).str(), "GUI:AoCWood");
	EXPECT_STREQ(m_rules->getIconName(2).str(), "AoCWoodIcon");
	EXPECT_EQ(m_rules->getOrder(3), 3);	// defaults to the definition index
}

TEST_F(AoCCostTest, RedefiningATypeUpdatesInPlace)
{
	defineStandardTypes();
	const Int index = m_rules->defineType("Wood", "GUI:Timber", "TimberIcon", 1200, FALSE, 7);
	EXPECT_EQ(index, 2);
	EXPECT_EQ(m_rules->getTypeCount(), 5);
	EXPECT_EQ(m_rules->getCashEquivalentPerMille(2), 1200);
	EXPECT_EQ(m_rules->getOrder(2), 7);
}

TEST_F(AoCCostTest, IsCashRenamesIndexZero)
{
	defineStandardTypes();
	const Int index = m_rules->defineType("Credits", "GUI:Credits", "CreditsIcon", 250, TRUE, -1);
	EXPECT_EQ(index, AOC_RESOURCE_CASH);
	EXPECT_EQ(m_rules->getTypeCount(), 5);
	EXPECT_EQ(m_rules->findType("Credits"), AOC_RESOURCE_CASH);
	EXPECT_EQ(m_rules->findType("Cash"), -1);
	EXPECT_EQ(m_rules->getCashEquivalentPerMille(AOC_RESOURCE_CASH), 1000) << "cash is the unit whatever the block says";
	EXPECT_THROW(m_rules->defineType("Wood", "", "", 1000, TRUE, -1), decltype(INI_INVALID_DATA)) << "a non-cash type cannot become cash";
}

TEST_F(AoCCostTest, ASecondIsCashTypeIsAnError)
{
	defineStandardTypes();
	EXPECT_EQ(m_rules->defineType("Credits", "GUI:Credits", "", 1000, TRUE, -1), AOC_RESOURCE_CASH);
	EXPECT_EQ(m_rules->defineType("credits", "GUI:Money", "", 1000, TRUE, -1), AOC_RESOURCE_CASH) << "the same name again redefines cash";
	EXPECT_STREQ(m_rules->getDisplayLabel(AOC_RESOURCE_CASH).str(), "GUI:Money");
	EXPECT_THROW(m_rules->defineType("Gold", "", "", 1000, TRUE, -1), decltype(INI_INVALID_DATA)) << "exactly one type is cash";
	EXPECT_EQ(m_rules->findType("Credits"), AOC_RESOURCE_CASH) << "the rejected block renames nothing";
	EXPECT_EQ(m_rules->findType("Gold"), -1);
	EXPECT_EQ(m_rules->getTypeCount(), 5);
}

TEST_F(AoCCostTest, AtMostEightTypes)
{
	for (Int i = 1; i < AOC_MAX_RESOURCES; ++i)
	{
		AsciiString name;
		name.format("Type%d", i);
		EXPECT_EQ(m_rules->defineType(name, "", "", 1000, FALSE, -1), i);
	}
	EXPECT_EQ(m_rules->getTypeCount(), AOC_MAX_RESOURCES);
	EXPECT_THROW(m_rules->defineType("OneTooMany", "", "", 1000, FALSE, -1), decltype(INI_INVALID_DATA));
	EXPECT_EQ(m_rules->getTypeCount(), AOC_MAX_RESOURCES);
}

TEST_F(AoCCostTest, DefaultVectorIsZero)
{
	AoCCost cost;
	EXPECT_TRUE(cost.isZero());
	EXPECT_FALSE(cost.hasNonCash());
	cost.amount[AOC_RESOURCE_CASH] = 100;
	EXPECT_FALSE(cost.isZero());
	EXPECT_FALSE(cost.hasNonCash()) << "a cash-only vector is the stock case";
	cost.amount[2] = 1;
	EXPECT_TRUE(cost.hasNonCash());
}

TEST_F(AoCCostTest, ParseTextAnySubsetAnyOrder)
{
	defineStandardTypes();
	AoCCost a, b;
	UnsignedInt maskA = 0, maskB = 0;
	AoCCost::parseText("Wood:250 Cash:100", a, &maskA);
	AoCCost::parseText("Cash:100   Wood:250", b, &maskB);
	EXPECT_EQ(a.amount[AOC_RESOURCE_CASH], 100u);
	EXPECT_EQ(a.amount[2], 250u);
	EXPECT_EQ(a.amount[1], 0u);
	EXPECT_EQ(a.amount[4], 0u);
	EXPECT_EQ(maskA, (1u << AOC_RESOURCE_CASH) | (1u << 2));
	EXPECT_TRUE(a == b);
	EXPECT_EQ(maskA, maskB);

	AoCCost woodOnly;
	UnsignedInt maskWood = 0;
	AoCCost::parseText("Wood:250", woodOnly, &maskWood);
	EXPECT_EQ(woodOnly.amount[AOC_RESOURCE_CASH], 0u);
	EXPECT_EQ(maskWood, 1u << 2) << "an absent cash component is distinguishable from Cash:0";

	AoCCost explicitZero;
	UnsignedInt maskZero = 0;
	AoCCost::parseText("Cash:0", explicitZero, &maskZero);
	EXPECT_TRUE(explicitZero.isZero());
	EXPECT_EQ(maskZero, 1u << AOC_RESOURCE_CASH);

	AoCCost reparsed = a;
	AoCCost::parseText("Steel:50", reparsed, nullptr);
	EXPECT_EQ(reparsed.amount[2], 0u) << "a second AoCCost line replaces, like every INI field";
	EXPECT_EQ(reparsed.amount[4], 50u);
}

TEST_F(AoCCostTest, ParseFromINILine)
{
	defineStandardTypes();
	AoCCost cost;
	UnsignedInt mask = 0;
	parseLine("AoCCost = Wood:250 Cash:100", cost, &mask);
	EXPECT_EQ(cost.amount[AOC_RESOURCE_CASH], 100u);
	EXPECT_EQ(cost.amount[2], 250u);
	EXPECT_EQ(mask, (1u << AOC_RESOURCE_CASH) | (1u << 2));

	AoCCost stock;
	parseLine("AoCStartingStock = Food:200 Wood:200 Cash:500", stock, nullptr);
	EXPECT_EQ(stock.amount[1], 200u);
	EXPECT_EQ(stock.amount[2], 200u);
	EXPECT_EQ(stock.amount[AOC_RESOURCE_CASH], 500u);

	AoCCost empty;
	empty.amount[3] = 9;
	parseLine("AoCCost =", empty, nullptr);
	EXPECT_TRUE(empty.isZero()) << "an empty value is a zero vector";
}

TEST_F(AoCCostTest, ParseRejectsBadInput)
{
	defineStandardTypes();
	AoCCost cost;
	EXPECT_THROW(AoCCost::parseText("Gold:5", cost, nullptr), decltype(INI_INVALID_DATA)) << "unknown type";
	EXPECT_THROW(AoCCost::parseText("Wood:-5", cost, nullptr), decltype(INI_INVALID_DATA)) << "negative amount";
	EXPECT_THROW(AoCCost::parseText("Wood", cost, nullptr), decltype(INI_INVALID_DATA)) << "no colon";
	EXPECT_THROW(AoCCost::parseText("Wood:", cost, nullptr), decltype(INI_INVALID_DATA)) << "no amount";
	EXPECT_THROW(AoCCost::parseText(":5", cost, nullptr), decltype(INI_INVALID_DATA)) << "no name";
	EXPECT_THROW(AoCCost::parseText("Wood:abc", cost, nullptr), decltype(INI_INVALID_DATA)) << "non-numeric amount";
}

TEST_F(AoCCostTest, WithoutRulesOnlyCashParses)
{
	TheAoCRules = nullptr;
	AoCCost cost;
	AoCCost::parseText("Cash:40", cost, nullptr);
	EXPECT_EQ(cost.amount[AOC_RESOURCE_CASH], 40u);
	EXPECT_THROW(AoCCost::parseText("Wood:5", cost, nullptr), decltype(INI_INVALID_DATA));
	EXPECT_EQ(AoCRules::typeCount(), 1);
	EXPECT_EQ(AoCRules::cashEquivalentPerMille(AOC_RESOURCE_CASH), 1000);
	EXPECT_EQ(AoCRules::cashEquivalentPerMille(1), 0);
	TheAoCRules = m_rules;
}

TEST_F(AoCCostTest, AddScaleAndEquality)
{
	defineStandardTypes();
	AoCCost a, b;
	AoCCost::parseText("Wood:250 Cash:100", a, nullptr);
	AoCCost::parseText("Wood:50 Steel:10", b, nullptr);
	a.add(b);
	EXPECT_EQ(a.amount[AOC_RESOURCE_CASH], 100u);
	EXPECT_EQ(a.amount[2], 300u);
	EXPECT_EQ(a.amount[4], 10u);

	AoCCost half = a;
	half.scale(500);
	EXPECT_EQ(half.amount[AOC_RESOURCE_CASH], 50u);
	EXPECT_EQ(half.amount[2], 150u);
	EXPECT_EQ(half.amount[4], 5u);

	AoCCost odd;
	AoCCost::parseText("Wood:3", odd, nullptr);
	odd.scale(500);
	EXPECT_EQ(odd.amount[2], 1u) << "integer arithmetic truncates";
	odd.scale(0);
	EXPECT_TRUE(odd.isZero());

	AoCCost c = a;
	EXPECT_TRUE(a == c);
	EXPECT_FALSE(a != c);
	c.amount[1] = 1;
	EXPECT_TRUE(a != c);
}

TEST_F(AoCCostTest, CashEquivalentWeighsByPerMille)
{
	defineStandardTypes();
	AoCCost cost;
	AoCCost::parseText("Cash:50 Food:100 Iron:10 Steel:10", cost, nullptr);
	// 50 + 100*1.0 + 10*1.5 + 10*4.0 = 205
	EXPECT_EQ(cost.cashEquivalent(), 205u);

	AoCCost cashOnly;
	AoCCost::parseText("Cash:777", cashOnly, nullptr);
	EXPECT_EQ(cashOnly.cashEquivalent(), 777u) << "a cash-only vector equals its cash";

	AoCCost fraction;
	AoCCost::parseText("Iron:1", fraction, nullptr);
	EXPECT_EQ(fraction.cashEquivalent(), 1u) << "1 * 1500 / 1000 truncates to 1";
}

TEST_F(AoCCostTest, ScriptComparisons)
{
	EXPECT_TRUE(AoCRules::compareInts(Parameter::LESS_THAN, 100, 150));
	EXPECT_FALSE(AoCRules::compareInts(Parameter::LESS_THAN, 150, 150));
	EXPECT_TRUE(AoCRules::compareInts(Parameter::LESS_EQUAL, 150, 150));
	EXPECT_TRUE(AoCRules::compareInts(Parameter::EQUAL, 150, 150));
	EXPECT_FALSE(AoCRules::compareInts(Parameter::EQUAL, 149, 150));
	EXPECT_TRUE(AoCRules::compareInts(Parameter::GREATER_EQUAL, 150, 150));
	EXPECT_TRUE(AoCRules::compareInts(Parameter::GREATER, 200, 150));
	EXPECT_FALSE(AoCRules::compareInts(Parameter::GREATER, 150, 150));
	EXPECT_TRUE(AoCRules::compareInts(Parameter::NOT_EQUAL, 1, 2));
	EXPECT_FALSE(AoCRules::compareInts(-1, 1, 2)) << "an unknown comparison is false";
}

// An INI fed from a string with a chosen load type, the way INI::load feeds a file: the block's first
// line is read and its keyword consumed, then the block parser reads the rest of the block.
class TextINI : public INI
{
public:
	TextINI(const char *text, INILoadType loadType)
	{
		const size_t length = strlen(text);
		m_readBuffer = new char[length];
		memcpy(m_readBuffer, text, length);
		m_readBufferNext = 0;
		m_readBufferUsed = (unsigned)length;
		m_filename = "test.ini";
		m_loadType = loadType;
	}
	~TextINI() { unPrepFile(); }

	void parseBlock(void (*parse)(INI *))
	{
		readLine();
		::strtok(m_buffer, getSeps());
		parse(this);
	}
};

TEST_F(AoCCostTest, MapIniCannotDefineResourceTypes)
{
	// Review finding 2: map.ini and solo.ini load with INI_LOAD_CREATE_OVERRIDES. A block there is
	// consumed and ignored on every machine, so a map cannot switch the feature on for the session.
	const char *block = "AoCResourceType Wood\n  DisplayName = GUI:AoCWood\n  CashEquivalent = 1200\nEnd\n";
	{
		TextINI mapIni(block, INI_LOAD_CREATE_OVERRIDES);
		mapIni.parseBlock(AoCRules::parseResourceTypeDefinition);
		EXPECT_EQ(mapIni.getLineNum(), 4u) << "the whole block is consumed";
	}
	EXPECT_FALSE(m_rules->isEnabled());
	EXPECT_EQ(m_rules->getTypeCount(), 1);
	EXPECT_EQ(m_rules->findType("Wood"), -1);

	// The global load (Data\INI\AoCResources.ini) defines it.
	{
		TextINI globalIni(block, INI_LOAD_OVERWRITE);
		globalIni.parseBlock(AoCRules::parseResourceTypeDefinition);
	}
	EXPECT_TRUE(m_rules->isEnabled());
	EXPECT_EQ(m_rules->findType("Wood"), 1);
	EXPECT_EQ(m_rules->getCashEquivalentPerMille(1), 1200);
	EXPECT_STREQ(m_rules->getDisplayLabel(1).str(), "GUI:AoCWood");

	// A map cannot redefine a global type either.
	{
		TextINI mapIni("AoCResourceType Wood\n  CashEquivalent = 5000\nEnd\n", INI_LOAD_CREATE_OVERRIDES);
		mapIni.parseBlock(AoCRules::parseResourceTypeDefinition);
	}
	EXPECT_EQ(m_rules->getCashEquivalentPerMille(1), 1200);
	EXPECT_EQ(m_rules->getTypeCount(), 2);
}

} // namespace
