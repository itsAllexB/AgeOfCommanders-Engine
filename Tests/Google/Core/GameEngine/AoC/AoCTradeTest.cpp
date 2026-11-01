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

// AoC F11: the pure parts of the import trades (design D11 section 2.11): the AoCTrade fields and their errors (both
// directions, a missing Cost, an unknown type, RequiresObject lists), the store's ids and redefinition, the queue decision
// (funds, requires, both, a full queue, not offered), the delivery frames from milliseconds at 30 and 60 Hz, the log text.

#include <stdio.h>
#include <string.h>
#include <string>
#include <gtest/gtest.h>

#include "AoC/AoCCost.h"
#include "AoC/AoCRules.h"
#include "AoC/AoCSimRate.h"
#include "AoC/AoCTrade.h"
#include "Common/GameCommon.h"
#include "Common/INI.h"

namespace
{

enum { CASH = AOC_RESOURCE_CASH, FOOD = 1, WOOD = 2, IRON = 3, STEEL = 4 };

class AoCTradeTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_rules = new AoCRules;
		m_rules->init();
		TheAoCRules = m_rules;
		m_rules->defineType("Food", "", "", 1000, FALSE, -1);
		m_rules->defineType("Wood", "", "", 1000, FALSE, -1);
		m_rules->defineType("Iron", "", "", 1500, FALSE, -1);
		m_rules->defineType("Steel", "", "", 4000, FALSE, -1);
		m_startRate = AoCSimRate::g_framesPerSecond;
		AoCSimRate::g_framesPerSecond = 30;
	}

	void TearDown() override
	{
		AoCSimRate::g_framesPerSecond = m_startRate;
		TheAoCRules = nullptr;
		delete m_rules;
	}

	// Drives a field procedure the way INI::initFromINI does: the key token is consumed, the rest of the line is read with
	// the static INI tokenizer; the procedures write through 'instance' (the template being read).
	static void parse( INIFieldParseProc proc, AoCTradeTemplate &t, const char *line )
	{
		char buffer[256];
		strncpy(buffer, line, sizeof(buffer) - 1);
		buffer[sizeof(buffer) - 1] = '\0';
		::strtok(buffer, INI::getSeps());
		proc(nullptr, &t, &t, nullptr);
	}

	// The procedure of a key in AoCTradeTemplate::getFieldParse(), so the test reads the table the engine reads.
	static INIFieldParseProc procOf( const char *key )
	{
		for (const FieldParse *f = AoCTradeTemplate::getFieldParse(); f->token; ++f)
			if (strcmp(f->token, key) == 0)
				return f->parse;
		return nullptr;
	}

	// D2's exchange table as data: the USA lot of 300 wood at 1.75 x, delivered in 120 s.
	static AoCTradeTemplate buyWood()
	{
		AoCTradeTemplate t;
		parse(procOf("Direction"), t, "Direction = Buy");
		parse(procOf("Cost"), t, "Cost = Cash:525");
		parse(procOf("Payout"), t, "Payout = Wood:300");
		parse(procOf("DeliveryTime"), t, "DeliveryTime = 120000");
		t.finish();
		return t;
	}

	AoCRules *m_rules;
	Int m_startRate;
};

}	// namespace

TEST_F(AoCTradeTest, BuyAndSellParseIntoTwoVectors)
{
	const AoCTradeTemplate buy = buyWood();
	EXPECT_EQ(buy.getDirection(), AOC_TRADE_BUY);
	AoCCost cost, payout;
	cost.amount[CASH] = 525;
	payout.amount[WOOD] = 300;
	EXPECT_TRUE(buy.getCost() == cost) << "withdrawn at queue time";
	EXPECT_TRUE(buy.getPayout() == payout) << "deposited at delivery";
	EXPECT_EQ(buy.getDeliveryMsecs(), 120000u);
	EXPECT_TRUE(buy.getRequiredObjectNames().empty()) << "no RequiresObject: always unlocked";

	// Sell: the resource is the cost and cash the payout (D11 2.11 as amended: the two-vector form).
	AoCTradeTemplate sell;
	parse(procOf("Direction"), sell, "Direction = Sell");
	parse(procOf("Cost"), sell, "Cost = Wood:300");
	parse(procOf("Payout"), sell, "Payout = Cash:150");
	parse(procOf("DeliveryTime"), sell, "DeliveryTime = 120000");
	sell.finish();
	EXPECT_EQ(sell.getDirection(), AOC_TRADE_SELL);
	EXPECT_EQ(sell.getCost().amount[WOOD], 300u);
	EXPECT_EQ(sell.getPayout().amount[CASH], 150u);
	EXPECT_FALSE(sell.getPayout().hasNonCash());

	// The direction is case-insensitive, as stock lookups are; the optional names are kept as written.
	AoCTradeTemplate lower;
	parse(procOf("Direction"), lower, "Direction = sell");
	EXPECT_EQ(lower.getDirection(), AOC_TRADE_SELL);
}

TEST_F(AoCTradeTest, MissingOrBadFieldsAreIniErrors)
{
	AoCTradeTemplate noCost;
	parse(procOf("Direction"), noCost, "Direction = Buy");
	parse(procOf("Payout"), noCost, "Payout = Wood:300");
	parse(procOf("DeliveryTime"), noCost, "DeliveryTime = 120000");
	EXPECT_ANY_THROW(noCost.finish()) << "a missing Cost";

	AoCTradeTemplate noPayout;
	parse(procOf("Direction"), noPayout, "Direction = Buy");
	parse(procOf("Cost"), noPayout, "Cost = Cash:525");
	parse(procOf("DeliveryTime"), noPayout, "DeliveryTime = 120000");
	EXPECT_ANY_THROW(noPayout.finish()) << "a missing Payout";

	AoCTradeTemplate noTime;
	parse(procOf("Direction"), noTime, "Direction = Buy");
	parse(procOf("Cost"), noTime, "Cost = Cash:525");
	parse(procOf("Payout"), noTime, "Payout = Wood:300");
	EXPECT_ANY_THROW(noTime.finish()) << "a missing DeliveryTime";

	AoCTradeTemplate noDirection;
	parse(procOf("Cost"), noDirection, "Cost = Cash:525");
	parse(procOf("Payout"), noDirection, "Payout = Wood:300");
	parse(procOf("DeliveryTime"), noDirection, "DeliveryTime = 120000");
	EXPECT_ANY_THROW(noDirection.finish()) << "a missing Direction";

	AoCTradeTemplate zero;
	parse(procOf("Direction"), zero, "Direction = Buy");
	parse(procOf("Cost"), zero, "Cost = Cash:0");
	parse(procOf("Payout"), zero, "Payout = Wood:300");
	parse(procOf("DeliveryTime"), zero, "DeliveryTime = 120000");
	EXPECT_ANY_THROW(zero.finish()) << "a zero Cost is a missing one";

	AoCTradeTemplate t;
	EXPECT_ANY_THROW(parse(procOf("Cost"), t, "Cost = Gold:5")) << "an unknown type";
	EXPECT_ANY_THROW(parse(procOf("Payout"), t, "Payout = Gold:5")) << "an unknown type";
	EXPECT_ANY_THROW(parse(procOf("Cost"), t, "Cost = Cash:-5")) << "amounts are never negative";
	EXPECT_ANY_THROW(parse(procOf("Direction"), t, "Direction = Swap"));
	EXPECT_ANY_THROW(parse(procOf("DeliveryTime"), t, "DeliveryTime = 0"));
	EXPECT_ANY_THROW(parse(procOf("DeliveryTime"), t, "DeliveryTime = -100"));
	EXPECT_ANY_THROW(parse(procOf("RequiresObject"), t, "RequiresObject =")) << "at least one name";
}

TEST_F(AoCTradeTest, RequiresObjectIsAnAnyOfListAcrossLines)
{
	AoCTradeTemplate steel = buyWood();
	parse(procOf("RequiresObject"), steel, "RequiresObject = AmericaSteelMill ChinaSteelMill");
	parse(procOf("RequiresObject"), steel, "RequiresObject = GLAJunkYard");
	ASSERT_EQ(steel.getRequiredObjectNames().size(), 3u) << "a second line adds to the list";
	EXPECT_STREQ(steel.getRequiredObjectNames()[0].str(), "AmericaSteelMill");
	EXPECT_STREQ(steel.getRequiredObjectNames()[1].str(), "ChinaSteelMill");
	EXPECT_STREQ(steel.getRequiredObjectNames()[2].str(), "GLAJunkYard");
	// Without a player nothing can stand: a listed requirement is unmet; without a list the trade is always unlocked.
	EXPECT_FALSE(AoCTrade::requiresMet(&steel, nullptr));
	const AoCTradeTemplate wood = buyWood();
	EXPECT_TRUE(AoCTrade::requiresMet(&wood, nullptr));
}

TEST_F(AoCTradeTest, DeliveryFramesFollowTheRate)
{
	// 120 s is 3600 frames at 30 Hz and 7200 at 60 Hz, through the F15 helper, so both deliver at the same game second.
	AoCSimRate::g_framesPerSecond = 30;
	EXPECT_EQ(buyWood().getDeliveryFrames(), 3600u);
	EXPECT_EQ(AoCTrade::deliveryFrames(120000), 3600u);
	EXPECT_EQ(AoCTrade::deliveryFrames(150000), 4500u) << "China's train, 150 s";
	EXPECT_EQ(AoCTrade::deliveryFrames(45000), 1350u) << "the Black Market, 45 s";
	AoCSimRate::g_framesPerSecond = 60;
	EXPECT_EQ(buyWood().getDeliveryFrames(), 7200u);
	EXPECT_EQ(AoCTrade::deliveryFrames(45000), 2700u);
	// 1 ms is still a whole frame (the INI duration rule rounds up), never 0.
	EXPECT_EQ(AoCTrade::deliveryFrames(1), 1u);
	AoCSimRate::g_framesPerSecond = 30;
	EXPECT_EQ(AoCTrade::deliveryFrames(1), 1u);
	EXPECT_EQ(AoCTrade::deliveryFrames(34), 2u) << "34 ms at 30 Hz is 1.02 frames, rounded up";
}

TEST_F(AoCTradeTest, QueueDecisionOrder)
{
	AoCTradeQueueFacts ok;
	EXPECT_EQ(AoCTrade::decide(ok), AOC_TRADE_OK);

	AoCTradeQueueFacts funds;
	funds.canAfford = FALSE;
	EXPECT_EQ(AoCTrade::decide(funds), AOC_TRADE_REFUSED_FUNDS);

	AoCTradeQueueFacts locked;
	locked.requiresMet = FALSE;
	EXPECT_EQ(AoCTrade::decide(locked), AOC_TRADE_REFUSED_REQUIRES);

	AoCTradeQueueFacts both;
	both.requiresMet = FALSE;
	both.canAfford = FALSE;
	EXPECT_EQ(AoCTrade::decide(both), AOC_TRADE_REFUSED_REQUIRES) << "the lock is reported before the price (the button is greyed for it)";

	AoCTradeQueueFacts full;
	full.queueCount = 9;
	full.queueMax = 9;
	EXPECT_EQ(AoCTrade::decide(full), AOC_TRADE_REFUSED_QUEUE_FULL);
	full.canAfford = FALSE;
	EXPECT_EQ(AoCTrade::decide(full), AOC_TRADE_REFUSED_FUNDS) << "funds before a full queue, as canMakeUnit orders them";
	full.queueCount = 8;
	full.canAfford = TRUE;
	EXPECT_EQ(AoCTrade::decide(full), AOC_TRADE_OK) << "the last free slot";

	AoCTradeQueueFacts notOffered;
	notOffered.offered = FALSE;
	notOffered.requiresMet = FALSE;
	notOffered.canAfford = FALSE;
	EXPECT_EQ(AoCTrade::decide(notOffered), AOC_TRADE_REFUSED_NOT_OFFERED) << "a building without the button refuses first";

	EXPECT_STREQ(AoCTrade::refusalName(AOC_TRADE_REFUSED_FUNDS), "funds");
	EXPECT_STREQ(AoCTrade::refusalName(AOC_TRADE_REFUSED_REQUIRES), "requires");
	EXPECT_STREQ(AoCTrade::refusalName(AOC_TRADE_REFUSED_QUEUE_FULL), "queue-full");
	EXPECT_STREQ(AoCTrade::refusalName(AOC_TRADE_REFUSED_NOT_OFFERED), "not-offered");
}

TEST_F(AoCTradeTest, StoreGivesLoadOrderIdsAndRedefinesInPlace)
{
	AoCTradeStore store;
	const AoCTradeTemplate *wood = store.defineTrade("AoCTestBuyWood", buyWood());
	AoCTradeTemplate sell;
	parse(procOf("Direction"), sell, "Direction = Sell");
	parse(procOf("Cost"), sell, "Cost = Wood:300");
	parse(procOf("Payout"), sell, "Payout = Cash:150");
	parse(procOf("DeliveryTime"), sell, "DeliveryTime = 120000");
	sell.finish();
	const AoCTradeTemplate *sold = store.defineTrade("AoCTestSellWood", sell);
	ASSERT_EQ(store.getCount(), 2);
	EXPECT_EQ(wood->getID(), 0) << "ids in load order, as template ids";
	EXPECT_EQ(sold->getID(), 1);
	EXPECT_EQ(store.findTradeByID(1), sold);
	EXPECT_EQ(store.findTradeByID(2), nullptr);
	EXPECT_EQ(store.findTradeByID(-1), nullptr);
	EXPECT_EQ(store.findTrade("aoctestbuywood"), wood) << "names are found case-insensitively";
	EXPECT_EQ(store.findTrade("AoCTestNothing"), nullptr);
	EXPECT_STREQ(wood->getName().str(), "AoCTestBuyWood");

	// A second block with the same name replaces the data and keeps the id (a later file overriding an earlier one).
	AoCTradeTemplate dearer = buyWood();
	parse(procOf("Cost"), dearer, "Cost = Cash:600");
	dearer.finish();
	const AoCTradeTemplate *again = store.defineTrade("AoCTestBuyWood", dearer);
	EXPECT_EQ(again, wood);
	EXPECT_EQ(store.getCount(), 2);
	EXPECT_EQ(again->getID(), 0);
	EXPECT_EQ(again->getCost().amount[CASH], 600u);

	// Without a store (the base Generals game, tools) the helpers find nothing.
	EXPECT_EQ(AoCTradeStore::count(), 0);
	EXPECT_EQ(AoCTradeStore::find("AoCTestBuyWood"), nullptr);
}

TEST_F(AoCTradeTest, VectorTextForTheLog)
{
	char text[128];
	AoCTrade::vectorText(buyWood().getCost(), text, sizeof(text));
	EXPECT_STREQ(text, "Cash:525");
	AoCCost both;
	both.amount[CASH] = 150;
	both.amount[STEEL] = 20;
	AoCTrade::vectorText(both, text, sizeof(text));
	EXPECT_STREQ(text, "Cash:150 Steel:20");
	AoCTrade::vectorText(AoCCost(), text, sizeof(text));
	EXPECT_STREQ(text, "none");
}
