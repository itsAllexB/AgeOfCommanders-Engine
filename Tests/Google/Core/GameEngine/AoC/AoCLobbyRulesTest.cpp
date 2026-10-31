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

// AoC F7: the lobby choice lists, the options string keys and the index-to-value mapping (design D11 section 2.7).
// GameInfoToAsciiString and ParseAsciiStringToGameInfo need TheGameState and TheMapCache, which the test executable
// does not have; they call appendOptionsString and parseOptionsKey for the AoC keys, so the round trip is tested on
// those, and the game runs (gates b, c and f) cover the glue.

#include <string.h>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "AoC/AoCLobbyRules.h"
#include "AoC/AoCRules.h"
#include "Common/INI.h"
#include "Common/UserPreferences.h"

namespace
{

// An INI fed from a string with a chosen load type, the way INI::load feeds a file (as in AoCCostTest).
class LobbyTextINI : public INI
{
public:
	LobbyTextINI(const char *text, INILoadType loadType)
	{
		const size_t length = strlen(text);
		m_readBuffer = new char[length];
		memcpy(m_readBuffer, text, length);
		m_readBufferNext = 0;
		m_readBufferUsed = (unsigned)length;
		m_filename = "test.ini";
		m_loadType = loadType;
	}
	~LobbyTextINI() { unPrepFile(); }

	void parseBlocks(void (*parse)(INI *), Int count)
	{
		for (Int i = 0; i < count; ++i)
		{
			readLine();
			::strtok(m_buffer, getSeps());
			parse(this);
		}
	}
};

// The test overlay's lists (mod/test-f7): D2 section 7's three stocks, 600/1000/1600 richness, caps 12, 20, unlimited.
const char *const STOCK_BLOCKS =
	"AoCStartingStockChoice\n  Name = GUI:AoCStockLow\n  Value = Food:100 Wood:100 Steel:0 Cash:250\nEnd\n"
	"AoCStartingStockChoice\n  Name = GUI:AoCStockStandard\n  Value = Food:200 Wood:200 Steel:0 Cash:500\n  Default = Yes\nEnd\n"
	"AoCStartingStockChoice\n  Name = GUI:AoCStockHigh\n  Value = Food:500 Wood:500 Steel:100 Cash:1500\nEnd\n";
const char *const RICHNESS_BLOCKS =
	"AoCRichnessChoice\n  Name = GUI:AoCRichnessPoor\n  ScalePerMille = 600\nEnd\n"
	"AoCRichnessChoice\n  Name = GUI:AoCRichnessStandard\n  ScalePerMille = 1000\n  Default = Yes\nEnd\n"
	"AoCRichnessChoice\n  Name = GUI:AoCRichnessRich\n  ScalePerMille = 1600\nEnd\n";
const char *const CAP_BLOCKS =
	"AoCPopulationCapChoice\n  Name = GUI:AoCPopStandard\n  Cap = 12\n  Default = Yes\nEnd\n"
	"AoCPopulationCapChoice\n  Name = GUI:AoCPopHigh\n  Cap = 20\nEnd\n"
	"AoCPopulationCapChoice\n  Name = GUI:AoCPopUnlimited\n  Cap = -1\nEnd\n";

class AoCLobbyRulesTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_rules = new AoCRules;
		m_rules->init();
		TheAoCRules = m_rules;
		m_rules->defineType("Food", "GUI:AoCFood", "", 1000, FALSE, -1);
		m_rules->defineType("Wood", "GUI:AoCWood", "", 1000, FALSE, -1);
		m_rules->defineType("Iron", "GUI:AoCIron", "", 1500, FALSE, -1);
		m_rules->defineType("Steel", "GUI:AoCSteel", "", 4000, FALSE, -1);
		m_lobby = new AoCLobbyRules;
		TheAoCLobbyRules = m_lobby;
	}

	void TearDown() override
	{
		TheAoCLobbyRules = nullptr;
		delete m_lobby;
		TheAoCRules = nullptr;
		delete m_rules;
	}

	void loadTestLists()
	{
		{ LobbyTextINI ini(STOCK_BLOCKS, INI_LOAD_OVERWRITE); ini.parseBlocks(AoCLobbyRules::parseStartingStockChoiceDefinition, 3); }
		{ LobbyTextINI ini(RICHNESS_BLOCKS, INI_LOAD_OVERWRITE); ini.parseBlocks(AoCLobbyRules::parseRichnessChoiceDefinition, 3); }
		{ LobbyTextINI ini(CAP_BLOCKS, INI_LOAD_OVERWRITE); ini.parseBlocks(AoCLobbyRules::parsePopulationCapChoiceDefinition, 3); }
	}

	// Split "K=V;K=V;" and feed every pair to parseOptionsKey, as ParseAsciiStringToGameInfo does after the stock keys.
	// Returns FALSE when a key is not an AoC key or a value is malformed.
	static Bool parseKeys(const char *text, AoCLobbySettings &s)
	{
		std::string all(text);
		size_t start = 0;
		while (start < all.size())
		{
			size_t end = all.find(';', start);
			if (end == std::string::npos)
				end = all.size();
			const std::string pair = all.substr(start, end - start);
			start = end + 1;
			if (pair.empty())
				continue;
			const size_t eq = pair.find('=');
			if (eq == std::string::npos)
				return FALSE;
			Bool ok = TRUE;
			if (!AoCLobbyRules::parseOptionsKey(AsciiString(pair.substr(0, eq).c_str()), AsciiString(pair.substr(eq + 1).c_str()), s, ok) || !ok)
				return FALSE;
		}
		return TRUE;
	}

	static std::string written(const AoCLobbySettings &s)
	{
		AsciiString out;
		AoCLobbyRules::appendOptionsString(s, out);
		return std::string(out.str());
	}

	AoCRules *m_rules;
	AoCLobbyRules *m_lobby;
};

// --- no list defined: every field at its default, no key, the GameData.ini sources stay --------------------------------

TEST_F(AoCLobbyRulesTest, NoListsKeepTheStockGame)
{
	EXPECT_FALSE(AoCLobbyRules::anyListDefined());
	const AoCLobbySettings d = AoCLobbyRules::defaults();
	EXPECT_EQ(d.startingStock, 0);
	EXPECT_EQ(d.richness, 0);
	EXPECT_EQ(d.populationCap, 0);
	EXPECT_TRUE(d.ambientCivilians);
	EXPECT_EQ(d.victory, (Int)AOC_VICTORY_CONQUEST);
	EXPECT_EQ(written(d), "") << "a default game writes no AoC key: the string stays stock-readable";

	AoCCost gameData;
	AoCCost::parseText("Food:200 Wood:200 Cash:500", gameData);
	EXPECT_EQ(AoCLobbyRules::populationCapFor(d, 7), 7) << "F4's AoCDefaultPopulationCap stays the source";
	EXPECT_EQ(AoCLobbyRules::richnessPerMilleFor(d), 1000);
	EXPECT_TRUE(AoCLobbyRules::startingStockDepositFor(d, gameData) == gameData) << "F1's AoCStartingStock stays the deposit, cash included";
	UnsignedInt cash = 12345;
	EXPECT_FALSE(AoCLobbyRules::startingCashFor(d, cash));
	EXPECT_EQ(cash, 12345u) << "the starting cash is left alone";

	// The same with no singleton at all (the base Generals game, tools).
	TheAoCLobbyRules = nullptr;
	EXPECT_FALSE(AoCLobbyRules::anyListDefined());
	EXPECT_TRUE(AoCLobbyRules::defaults() == d);
	EXPECT_EQ(AoCLobbyRules::populationCapFor(d, 0), 0);
	AoCLobbySettings odd = d;
	odd.startingStock = 2;
	EXPECT_EQ(AoCLobbyRules::sanitize(odd).startingStock, 0) << "with no list an index is the default";
	TheAoCLobbyRules = m_lobby;
}

// --- the INI blocks ------------------------------------------------------------------------------------------------------

TEST_F(AoCLobbyRulesTest, ChoiceListsParseFromINI)
{
	loadTestLists();
	ASSERT_EQ(m_lobby->getStartingStockCount(), 3);
	ASSERT_EQ(m_lobby->getRichnessCount(), 3);
	ASSERT_EQ(m_lobby->getPopulationCapCount(), 3);
	EXPECT_TRUE(AoCLobbyRules::anyListDefined());

	EXPECT_STREQ(m_lobby->getStartingStock(2)->name.str(), "GUI:AoCStockHigh");
	const AoCCost &high = m_lobby->getStartingStock(2)->value;
	EXPECT_EQ(high.amount[m_rules->findType("Food")], 500u);
	EXPECT_EQ(high.amount[m_rules->findType("Wood")], 500u);
	EXPECT_EQ(high.amount[m_rules->findType("Steel")], 100u);
	EXPECT_EQ(high.amount[AOC_RESOURCE_CASH], 1500u);
	EXPECT_EQ(m_lobby->getRichness(0)->scalePerMille, 600);
	EXPECT_EQ(m_lobby->getRichness(2)->scalePerMille, 1600);
	EXPECT_EQ(m_lobby->getPopulationCap(0)->cap, 12);
	EXPECT_EQ(m_lobby->getPopulationCap(2)->cap, -1) << "Unlimited";
	EXPECT_EQ(m_lobby->getStartingStock(3), nullptr);
	EXPECT_EQ(m_lobby->getRichness(-1), nullptr);

	const AoCLobbySettings d = AoCLobbyRules::defaults();
	EXPECT_EQ(d.startingStock, 1) << "Default = Yes picks Standard";
	EXPECT_EQ(d.richness, 1);
	EXPECT_EQ(d.populationCap, 0);
	EXPECT_EQ(written(d), "");
}

TEST_F(AoCLobbyRulesTest, WithoutADefaultTheFirstChoiceIsTheDefault)
{
	{ LobbyTextINI ini("AoCRichnessChoice\n  Name = A\n  ScalePerMille = 700\nEnd\nAoCRichnessChoice\n  Name = B\n  ScalePerMille = 900\nEnd\n", INI_LOAD_OVERWRITE);
	  ini.parseBlocks(AoCLobbyRules::parseRichnessChoiceDefinition, 2); }
	EXPECT_EQ(AoCLobbyRules::defaults().richness, 0);
	EXPECT_EQ(AoCLobbyRules::richnessPerMilleFor(AoCLobbyRules::defaults()), 700);
}

TEST_F(AoCLobbyRulesTest, ListsRejectAmbiguousOrBadData)
{
	AoCCost v;
	m_lobby->addStartingStockChoice("GUI:A", v, TRUE);
	EXPECT_THROW(m_lobby->addStartingStockChoice("GUI:B", v, TRUE), decltype(INI_INVALID_DATA)) << "one Default per list";
	EXPECT_THROW(m_lobby->addStartingStockChoice("", v, FALSE), decltype(INI_INVALID_DATA)) << "a name is required";
	EXPECT_THROW(m_lobby->addRichnessChoice("GUI:R", 0, FALSE), decltype(INI_INVALID_DATA)) << "richness of at least 1 per mille";
	EXPECT_THROW(m_lobby->addPopulationCapChoice("GUI:P", -2, FALSE), decltype(INI_INVALID_DATA)) << "-1 is the lowest cap";
	EXPECT_EQ(m_lobby->addPopulationCapChoice("GUI:P0", 0, FALSE), 0) << "0: no lobby clamp";
	for (Int i = 1; i < AOC_LOBBY_MAX_CHOICES; ++i)
		m_lobby->addPopulationCapChoice("GUI:P", 10 * i, FALSE);
	EXPECT_THROW(m_lobby->addPopulationCapChoice("GUI:P", 999, FALSE), decltype(INI_INVALID_DATA)) << "at most 16 choices";
	EXPECT_EQ(m_lobby->getPopulationCapCount(), (Int)AOC_LOBBY_MAX_CHOICES);

	LobbyTextINI ini("AoCStartingStockChoice\n  Name = GUI:X\n  Value = Gold:5\nEnd\n", INI_LOAD_OVERWRITE);
	EXPECT_ANY_THROW(ini.parseBlocks(AoCLobbyRules::parseStartingStockChoiceDefinition, 1)) << "a Value names defined resource types only";
}

TEST_F(AoCLobbyRulesTest, MapIniCannotAddChoices)
{
	// map.ini and solo.ini load with INI_LOAD_CREATE_OVERRIDES: stock rejects money choice overrides; here the block is
	// consumed and ignored on every machine, so a map never changes the list the next lobby indexes.
	{
		LobbyTextINI mapIni(CAP_BLOCKS, INI_LOAD_CREATE_OVERRIDES);
		mapIni.parseBlocks(AoCLobbyRules::parsePopulationCapChoiceDefinition, 3);
		EXPECT_EQ(mapIni.getLineNum(), 13u) << "the whole of every block is consumed";
	}
	EXPECT_EQ(m_lobby->getPopulationCapCount(), 0);
	EXPECT_FALSE(AoCLobbyRules::anyListDefined());
}

// --- index to value --------------------------------------------------------------------------------------------------------

TEST_F(AoCLobbyRulesTest, IndexToValueMapping)
{
	loadTestLists();
	AoCLobbySettings s = AoCLobbyRules::defaults();

	EXPECT_EQ(AoCLobbyRules::populationCapFor(s, 99), 12);
	s.populationCap = 1;
	EXPECT_EQ(AoCLobbyRules::populationCapFor(s, 99), 20);
	s.populationCap = 2;
	EXPECT_EQ(AoCLobbyRules::populationCapFor(s, 99), -1) << "Unlimited";

	s.richness = 0;
	EXPECT_EQ(AoCLobbyRules::richnessPerMilleFor(s), 600);
	s.richness = 2;
	EXPECT_EQ(AoCLobbyRules::richnessPerMilleFor(s), 1600);

	AoCCost gameData;
	AoCCost::parseText("Food:9 Wood:9 Cash:9", gameData);
	s.startingStock = 2;
	const AoCCost deposit = AoCLobbyRules::startingStockDepositFor(s, gameData);
	EXPECT_EQ(deposit.amount[m_rules->findType("Food")], 500u) << "the chosen vector replaces GameData.ini's";
	EXPECT_EQ(deposit.amount[m_rules->findType("Steel")], 100u);
	EXPECT_EQ(deposit.amount[AOC_RESOURCE_CASH], 0u) << "the cash arrives as the starting cash, not twice";
	UnsignedInt cash = 0;
	EXPECT_TRUE(AoCLobbyRules::startingCashFor(s, cash));
	EXPECT_EQ(cash, 1500u) << "High sets SC=1500";
	s.startingStock = 0;
	EXPECT_TRUE(AoCLobbyRules::startingCashFor(s, cash));
	EXPECT_EQ(cash, 250u);

	// An index outside its list (a hand-made string) is the default; victory outside the modes is conquest.
	AoCLobbySettings odd;
	odd.startingStock = 7;
	odd.richness = -3;
	odd.populationCap = 3;
	odd.victory = 9;
	const AoCLobbySettings fixed = AoCLobbyRules::sanitize(odd);
	EXPECT_EQ(fixed.startingStock, 1);
	EXPECT_EQ(fixed.richness, 1);
	EXPECT_EQ(fixed.populationCap, 0);
	EXPECT_EQ(fixed.victory, (Int)AOC_VICTORY_CONQUEST);
	EXPECT_EQ(AoCLobbyRules::populationCapFor(odd, 99), 12) << "the mapping sanitizes too";

	EXPECT_STREQ(AoCLobbyRules::startingStockName(2).str(), "GUI:AoCStockHigh");
	EXPECT_STREQ(AoCLobbyRules::richnessName(0).str(), "GUI:AoCRichnessPoor");
	EXPECT_STREQ(AoCLobbyRules::populationCapName(5).str(), "5") << "the index when there is no such choice";
	EXPECT_STREQ(AoCLobbyRules::victoryName(AOC_VICTORY_CONQUEST), "Conquest");
	EXPECT_STREQ(AoCLobbyRules::victoryName(AOC_VICTORY_SCORE), "Score");
}

TEST_F(AoCLobbyRulesTest, RichnessArithmeticIsIntegerPerMilleRoundedDownNeverBelowOne)
{
	EXPECT_EQ(AoCLobbyRules::scaleBoxes(40, 1600), 64);
	EXPECT_EQ(AoCLobbyRules::scaleBoxes(40, 600), 24);
	EXPECT_EQ(AoCLobbyRules::scaleBoxes(40, 1000), 40);
	EXPECT_EQ(AoCLobbyRules::scaleBoxes(7, 1500), 10) << "10.5 rounds down";
	EXPECT_EQ(AoCLobbyRules::scaleBoxes(3, 333), 1) << "0.999 would be empty: a node keeps one box";
	EXPECT_EQ(AoCLobbyRules::scaleBoxes(1, 600), 1);
	EXPECT_EQ(AoCLobbyRules::scaleBoxes(0, 1600), 0) << "an empty node stays empty";
	EXPECT_EQ(AoCLobbyRules::scaleBoxes(2000000, 1600), 3200000) << "64-bit intermediate, no overflow";
	EXPECT_EQ(AoCLobbyRules::scaleBoxes(2000000000, 1600), 0x7FFFFFFF) << "clamped to Int";
}

// --- the options string (decision 1) --------------------------------------------------------------------------------------

TEST_F(AoCLobbyRulesTest, OptionsStringRoundTripForTheFiveKeys)
{
	loadTestLists();
	AoCLobbySettings s;
	s.startingStock = 2;
	s.richness = 2;
	s.populationCap = 2;
	s.ambientCivilians = FALSE;
	s.victory = AOC_VICTORY_WONDER;
	const std::string text = written(s);
	EXPECT_EQ(text, "AS=2;AR=2;AP=2;AA=N;AV=1;");
	EXPECT_LE(text.size(), 25u) << "25 characters with all five keys at one-digit indices";

	AoCLobbySettings back = AoCLobbyRules::defaults();
	ASSERT_TRUE(parseKeys(text.c_str(), back));
	EXPECT_TRUE(back == s);

	// Only the fields that differ from their default are written.
	AoCLobbySettings one = AoCLobbyRules::defaults();
	one.startingStock = 2;
	one.ambientCivilians = FALSE;
	EXPECT_EQ(written(one), "AS=2;AA=N;");
	AoCLobbySettings oneBack = AoCLobbyRules::defaults();
	ASSERT_TRUE(parseKeys(written(one).c_str(), oneBack));
	EXPECT_TRUE(oneBack == one);

	// Two-digit indices (lists of up to 16) are the longest form.
	AoCLobbySettings wide = AoCLobbyRules::defaults();
	wide.populationCap = 15;
	EXPECT_EQ(written(wide), "AP=15;");
}

TEST_F(AoCLobbyRulesTest, NoKeysMeansDefaultsAndExplicitDefaultsParseTheSame)
{
	loadTestLists();
	AoCLobbySettings none = AoCLobbyRules::defaults();
	ASSERT_TRUE(parseKeys("", none));
	EXPECT_TRUE(none == AoCLobbyRules::defaults()) << "a stock string (no AoC key) is a default game";

	AoCLobbySettings explicitDefaults = AoCLobbyRules::defaults();
	ASSERT_TRUE(parseKeys("AS=1;AR=1;AP=0;AA=Y;AV=0;", explicitDefaults));
	EXPECT_TRUE(explicitDefaults == none);
	EXPECT_EQ(written(explicitDefaults), "") << "written back, explicit defaults disappear";

	// A stock key is not ours; malformed values are refused like a stock empty value.
	AoCLobbySettings s = AoCLobbyRules::defaults();
	Bool ok = TRUE;
	EXPECT_FALSE(AoCLobbyRules::parseOptionsKey("SC", "10000", s, ok));
	EXPECT_FALSE(AoCLobbyRules::parseOptionsKey("AF", "60", s, ok));
	EXPECT_TRUE(ok);
	EXPECT_TRUE(AoCLobbyRules::parseOptionsKey("AS", "x2", s, ok));
	EXPECT_FALSE(ok);
	ok = TRUE;
	EXPECT_TRUE(AoCLobbyRules::parseOptionsKey("AA", "maybe", s, ok));
	EXPECT_FALSE(ok);
	ok = TRUE;
	EXPECT_TRUE(AoCLobbyRules::parseOptionsKey("AP", "2 ", s, ok));
	EXPECT_FALSE(ok) << "trailing characters are malformed";
	EXPECT_TRUE(s == AoCLobbyRules::defaults()) << "a refused value changes nothing";
}

// --- remembered choices ------------------------------------------------------------------------------------------------------

TEST_F(AoCLobbyRulesTest, PreferencesRememberChoicesByName)
{
	UserPreferences none;
	AoCLobbySettings untouched = AoCLobbyRules::defaults();
	AoCLobbyRules::writePreferences(none, untouched);
	EXPECT_TRUE(none.empty()) << "without lists nothing new is written to a stock install's preferences";

	loadTestLists();
	AoCLobbySettings s = AoCLobbyRules::defaults();
	s.startingStock = 2;
	s.richness = 0;
	s.populationCap = 2;
	s.ambientCivilians = FALSE;
	UserPreferences prefs;
	AoCLobbyRules::writePreferences(prefs, s);
	EXPECT_STREQ(prefs["AoCStartingStock"].str(), "GUI:AoCStockHigh");
	EXPECT_STREQ(prefs["AoCRichness"].str(), "GUI:AoCRichnessPoor");
	EXPECT_STREQ(prefs["AoCPopulationCap"].str(), "GUI:AoCPopUnlimited");
	EXPECT_STREQ(prefs["AoCAmbientCivilians"].str(), "No");
	EXPECT_STREQ(prefs["AoCVictory"].str(), "Conquest");

	AoCLobbySettings back = AoCLobbyRules::defaults();
	AoCLobbyRules::readPreferences(prefs, back);
	EXPECT_TRUE(back == s);

	// A list reordered by a later mod version keeps the remembered choice; a choice that is gone is ignored.
	m_lobby->clear();
	AoCCost v;
	m_lobby->addStartingStockChoice("GUI:AoCStockHigh", v, FALSE);
	m_lobby->addStartingStockChoice("GUI:AoCStockStandard", v, TRUE);
	AoCLobbySettings reordered = AoCLobbyRules::defaults();
	AoCLobbyRules::readPreferences(prefs, reordered);
	EXPECT_EQ(reordered.startingStock, 0);
	EXPECT_EQ(reordered.richness, 0) << "the richness list is gone: the field keeps its default";
	EXPECT_FALSE(reordered.ambientCivilians);
}

} // namespace
