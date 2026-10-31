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

// AoC F1: the resource bank's afford, withdraw and refund logic, the script handler logic behind
// AOC_PLAYER_HAS_RESOURCE / AOC_PLAYER_GIVE_RESOURCE / AOC_PLAYER_SET_RESOURCE, and the CRC rule
// (design D11 section 2.1). The bank runs standalone here (no Money bound), so cash is its own slot.

#include <gtest/gtest.h>

#include "AoC/AoCCost.h"
#include "AoC/AoCResourceBank.h"
#include "AoC/AoCRules.h"
#include "Common/XferCRC.h"
#include "GameLogic/Scripts.h"

namespace
{

enum { FOOD = 1, WOOD = 2, IRON = 3, STEEL = 4 };

class AoCResourceBankTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_rules = new AoCRules;
		m_rules->init();
		TheAoCRules = m_rules;
		m_bank.init(nullptr);
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

	static AoCCost cost(const char *text)
	{
		AoCCost c;
		AoCCost::parseText(text, c, nullptr);
		return c;
	}

	// The light CRC of one bank, as Player::crc computes it through xferSnapshot.
	static UnsignedInt crcOf(AoCResourceBank &bank)
	{
		XferCRC xfer;
		xfer.open("lightCRC");
		xfer.xferSnapshot(&bank);
		UnsignedInt value = xfer.getCRC();
		xfer.close();
		return value;
	}

	AoCRules *m_rules;
	AoCResourceBank m_bank;
};

TEST_F(AoCResourceBankTest, StartsEmpty)
{
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		EXPECT_EQ(m_bank.count(i), 0u);
	EXPECT_EQ(m_bank.count(-1), 0u);
	EXPECT_EQ(m_bank.count(AOC_MAX_RESOURCES), 0u);
	EXPECT_TRUE(m_bank.canAfford(AoCCost())) << "a zero vector is always affordable";
}

TEST_F(AoCResourceBankTest, DepositAndCount)
{
	defineStandardTypes();
	m_bank.deposit(AOC_RESOURCE_CASH, 500, FALSE, FALSE);
	m_bank.deposit(WOOD, 200, FALSE, FALSE);
	m_bank.deposit(WOOD, 50);
	EXPECT_EQ(m_bank.count(AOC_RESOURCE_CASH), 500u);
	EXPECT_EQ(m_bank.count(WOOD), 250u);
	EXPECT_EQ(m_bank.count(FOOD), 0u);

	m_bank.deposit(cost("Food:200 Wood:200 Cash:500"), FALSE, FALSE);
	EXPECT_EQ(m_bank.count(AOC_RESOURCE_CASH), 1000u);
	EXPECT_EQ(m_bank.count(FOOD), 200u);
	EXPECT_EQ(m_bank.count(WOOD), 450u);
}

TEST_F(AoCResourceBankTest, CanAffordComparesEveryComponent)
{
	defineStandardTypes();
	m_bank.deposit(cost("Food:200 Wood:200 Cash:500"), FALSE, FALSE);
	EXPECT_TRUE(m_bank.canAfford(cost("Wood:200 Cash:100")));
	EXPECT_TRUE(m_bank.canAfford(cost("Cash:500")));
	EXPECT_FALSE(m_bank.canAfford(cost("Wood:250 Cash:100"))) << "the barracks needs 250 wood";
	EXPECT_FALSE(m_bank.canAfford(cost("Cash:501")));
	EXPECT_FALSE(m_bank.canAfford(cost("Steel:1")));
	m_bank.deposit(WOOD, 50);
	EXPECT_TRUE(m_bank.canAfford(cost("Wood:250 Cash:100"))) << "affordable once wood suffices";
}

TEST_F(AoCResourceBankTest, WithdrawIsAllOrNothingForNonCash)
{
	defineStandardTypes();
	m_bank.deposit(cost("Food:200 Wood:200 Cash:500"), FALSE, FALSE);
	EXPECT_FALSE(m_bank.withdraw(cost("Wood:250 Cash:100"), FALSE));
	EXPECT_EQ(m_bank.count(WOOD), 200u) << "nothing taken";
	EXPECT_EQ(m_bank.count(AOC_RESOURCE_CASH), 500u) << "not even the cash";

	EXPECT_TRUE(m_bank.withdraw(cost("Wood:200 Food:50 Cash:100"), FALSE));
	EXPECT_EQ(m_bank.count(WOOD), 0u);
	EXPECT_EQ(m_bank.count(FOOD), 150u);
	EXPECT_EQ(m_bank.count(AOC_RESOURCE_CASH), 400u);
}

TEST_F(AoCResourceBankTest, WithdrawTakesNothingWhenOnlyCashIsShort)
{
	// Review finding 1: the typed components are covered but cash is not. The whole vector is refused,
	// so a caller that does not check canAfford can never get the typed part for free or a clamped cash.
	defineStandardTypes();
	m_bank.deposit(cost("Food:50 Wood:300 Cash:100"), FALSE, FALSE);
	EXPECT_FALSE(m_bank.withdraw(cost("Wood:250 Cash:150"), FALSE));
	EXPECT_EQ(m_bank.count(WOOD), 300u) << "the wood stays";
	EXPECT_EQ(m_bank.count(AOC_RESOURCE_CASH), 100u) << "the cash is not clamped";
	EXPECT_EQ(m_bank.count(FOOD), 50u);

	EXPECT_FALSE(m_bank.withdraw(cost("Wood:250 Food:51 Cash:100"), FALSE)) << "any one short component refuses";
	EXPECT_EQ(m_bank.count(WOOD), 300u);
	EXPECT_EQ(m_bank.count(AOC_RESOURCE_CASH), 100u);

	EXPECT_TRUE(m_bank.withdraw(cost("Wood:250 Food:50 Cash:100"), FALSE)) << "exactly enough of everything";
	EXPECT_EQ(m_bank.count(WOOD), 50u);
	EXPECT_EQ(m_bank.count(FOOD), 0u);
	EXPECT_EQ(m_bank.count(AOC_RESOURCE_CASH), 0u);
}

TEST_F(AoCResourceBankTest, CashOnlyVectorKeepsTheStockClamp)
{
	defineStandardTypes();
	m_bank.deposit(cost("Wood:100 Cash:100"), FALSE, FALSE);
	// Stock Money::withdraw never goes into debt and takes what is there; a cash-only vector (every cost
	// while the feature is off) keeps exactly that, so the stock construct path is unchanged.
	EXPECT_FALSE(m_bank.withdraw(cost("Cash:150"), FALSE));
	EXPECT_EQ(m_bank.count(AOC_RESOURCE_CASH), 0u) << "clamped, as Money::withdraw does";
	EXPECT_EQ(m_bank.count(WOOD), 100u) << "the typed amounts are not touched";
	m_bank.deposit(AOC_RESOURCE_CASH, 40, FALSE, FALSE);
	EXPECT_TRUE(m_bank.withdraw(cost("Cash:40"), FALSE));
	EXPECT_EQ(m_bank.count(AOC_RESOURCE_CASH), 0u);
	m_bank.withdrawType(WOOD, 100, FALSE);
	EXPECT_EQ(m_bank.withdrawType(WOOD, 10, FALSE), 0u);
	m_bank.deposit(WOOD, 30);
	EXPECT_EQ(m_bank.withdrawType(WOOD, 100, FALSE), 30u) << "returns the amount actually taken";
	EXPECT_EQ(m_bank.count(WOOD), 0u);
}

TEST_F(AoCResourceBankTest, RefundRestoresTheVector)
{
	defineStandardTypes();
	const AoCCost barracks = cost("Wood:250 Cash:100");
	m_bank.deposit(cost("Food:200 Wood:300 Cash:500"), FALSE, FALSE);
	ASSERT_TRUE(m_bank.withdraw(barracks, FALSE));
	EXPECT_EQ(m_bank.count(WOOD), 50u);
	EXPECT_EQ(m_bank.count(AOC_RESOURCE_CASH), 400u);
	m_bank.deposit(barracks, TRUE, FALSE);	// cancel: full refund
	EXPECT_EQ(m_bank.count(WOOD), 300u);
	EXPECT_EQ(m_bank.count(AOC_RESOURCE_CASH), 500u);
	EXPECT_EQ(m_bank.count(FOOD), 200u);

	// sell: the non-cash share at AoCRefundPerMille (default 500), cash through the stock sell path
	AoCCost sellRefund = barracks;
	sellRefund.amount[AOC_RESOURCE_CASH] = 0;
	sellRefund.scale(500);
	EXPECT_EQ(sellRefund.amount[WOOD], 125u);
	EXPECT_EQ(sellRefund.amount[AOC_RESOURCE_CASH], 0u);
}

TEST_F(AoCResourceBankTest, Shortfall)
{
	defineStandardTypes();
	m_bank.deposit(cost("Wood:200 Cash:50"), FALSE, FALSE);
	AoCCost missing;
	EXPECT_TRUE(m_bank.shortfall(cost("Wood:250 Steel:10 Cash:100"), missing));
	EXPECT_EQ(missing.amount[WOOD], 50u);
	EXPECT_EQ(missing.amount[STEEL], 10u);
	EXPECT_EQ(missing.amount[AOC_RESOURCE_CASH], 50u);
	EXPECT_EQ(missing.amount[FOOD], 0u);
	EXPECT_FALSE(m_bank.shortfall(cost("Wood:200"), missing));
	EXPECT_TRUE(missing.isZero());
}

TEST_F(AoCResourceBankTest, CashEquivalentWeighsEveryType)
{
	// Feature off: exactly the cash on hand, so the -aiLog cash_equivalent column equals money.
	m_bank.deposit(AOC_RESOURCE_CASH, 1234, FALSE, FALSE);
	EXPECT_EQ(m_bank.cashEquivalent(), 1234u);

	defineStandardTypes();
	m_bank.deposit(cost("Food:100 Iron:10 Steel:10"), FALSE, FALSE);
	// 1234 + 100 + 15 + 40
	EXPECT_EQ(m_bank.cashEquivalent(), 1389u);
}

TEST_F(AoCResourceBankTest, DeathAndTransfer)
{
	defineStandardTypes();
	AoCResourceBank other;
	other.init(nullptr);
	m_bank.deposit(cost("Food:10 Wood:20 Cash:30"), FALSE, FALSE);
	other.deposit(cost("Wood:5 Steel:7 Cash:1"), FALSE, FALSE);

	other.transferNonCashFrom(m_bank);	// the stock lines move the cash
	EXPECT_EQ(other.count(FOOD), 10u);
	EXPECT_EQ(other.count(WOOD), 25u);
	EXPECT_EQ(other.count(STEEL), 7u);
	EXPECT_EQ(other.count(AOC_RESOURCE_CASH), 1u);
	EXPECT_EQ(m_bank.count(FOOD), 0u);
	EXPECT_EQ(m_bank.count(WOOD), 0u);
	EXPECT_EQ(m_bank.count(AOC_RESOURCE_CASH), 30u);

	other.clearNonCash();	// the stock line zeroes the cash on death
	EXPECT_EQ(other.count(WOOD), 0u);
	EXPECT_EQ(other.count(STEEL), 0u);
	EXPECT_EQ(other.count(AOC_RESOURCE_CASH), 1u);
}

// --- script handler logic: AOC_PLAYER_HAS_RESOURCE (int, comparison, side, resource) ----------------

static Bool playerHasResource(const AoCResourceBank &bank, Int amount, Int comparison, const char *resourceName)
{
	// The same steps as ScriptConditions::evaluateAoCPlayerHasResource after the player lookup.
	Int type = AoCRules::findTypeIndex(resourceName);
	if (type < 0)
		return FALSE;
	return AoCRules::compareInts(comparison, amount, (Int)bank.count(type));
}

TEST_F(AoCResourceBankTest, ScriptConditionHasResource)
{
	defineStandardTypes();
	m_bank.deposit(cost("Wood:150 Cash:40"), FALSE, FALSE);
	// "X is <comparison> the amount possessed by <player> of <resource>", the PLAYER_HAS_CREDITS orientation
	EXPECT_TRUE(playerHasResource(m_bank, 100, Parameter::LESS_THAN, "Wood"));
	EXPECT_FALSE(playerHasResource(m_bank, 200, Parameter::LESS_THAN, "Wood"));
	EXPECT_TRUE(playerHasResource(m_bank, 150, Parameter::EQUAL, "wood"));
	EXPECT_TRUE(playerHasResource(m_bank, 200, Parameter::GREATER, "Wood"));
	EXPECT_TRUE(playerHasResource(m_bank, 40, Parameter::GREATER_EQUAL, "Cash"));
	EXPECT_TRUE(playerHasResource(m_bank, 0, Parameter::EQUAL, "Steel"));
	EXPECT_FALSE(playerHasResource(m_bank, 0, Parameter::EQUAL, "Gold")) << "an unknown type is false, never a crash";
}

// --- script handler logic: AOC_PLAYER_GIVE_RESOURCE and AOC_PLAYER_SET_RESOURCE (side, resource, int) ---

static void giveResource(AoCResourceBank &bank, const char *resourceName, Int amount)
{
	Int type = AoCRules::findTypeIndex(resourceName);
	if (type < 0)
		return;
	if (amount < 0)
		bank.withdrawType(type, -amount, FALSE);
	else
		bank.deposit(type, amount, FALSE, FALSE);
}

static void setResource(AoCResourceBank &bank, const char *resourceName, Int amount)
{
	Int type = AoCRules::findTypeIndex(resourceName);
	if (type < 0)
		return;
	bank.setAmount(type, amount < 0 ? 0 : amount);
}

TEST_F(AoCResourceBankTest, ScriptActionsGiveAndSet)
{
	defineStandardTypes();
	giveResource(m_bank, "Wood", 300);
	EXPECT_EQ(m_bank.count(WOOD), 300u);
	giveResource(m_bank, "Wood", -100);
	EXPECT_EQ(m_bank.count(WOOD), 200u);
	giveResource(m_bank, "Wood", -1000);
	EXPECT_EQ(m_bank.count(WOOD), 0u) << "taking more than there is empties the type, like Money";
	giveResource(m_bank, "Gold", 50);	// unknown: no effect
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		EXPECT_EQ(m_bank.count(i), 0u);

	giveResource(m_bank, "Cash", 70);
	EXPECT_EQ(m_bank.count(AOC_RESOURCE_CASH), 70u);
	setResource(m_bank, "Cash", 5);
	EXPECT_EQ(m_bank.count(AOC_RESOURCE_CASH), 5u);
	setResource(m_bank, "Steel", 42);
	EXPECT_EQ(m_bank.count(STEEL), 42u);
	setResource(m_bank, "Steel", -3);
	EXPECT_EQ(m_bank.count(STEEL), 0u);
}

// --- CRC rule: nothing enters the per-frame CRC while the feature is off ---------------------------

TEST_F(AoCResourceBankTest, CrcIsSilentWhileDisabledAndCoversAmountsWhenEnabled)
{
	AoCResourceBank empty;
	empty.init(nullptr);
	const UnsignedInt emptyCRC = crcOf(empty);

	m_bank.deposit(AOC_RESOURCE_CASH, 999, FALSE, FALSE);
	EXPECT_EQ(crcOf(m_bank), emptyCRC) << "feature off: the bank adds nothing, like the stock Money::crc";

	defineStandardTypes();
	const UnsignedInt cashOnlyCRC = crcOf(m_bank);
	EXPECT_NE(cashOnlyCRC, emptyCRC) << "feature on: cash enters the CRC";
	m_bank.deposit(WOOD, 1, FALSE, FALSE);
	EXPECT_NE(crcOf(m_bank), cashOnlyCRC) << "feature on: every typed amount enters the CRC";
}

} // namespace
