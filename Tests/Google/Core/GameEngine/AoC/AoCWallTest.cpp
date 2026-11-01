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

// AoC F10: the pure parts of the per-segment wall charge (design D11 section 2.10): how many segments a bank can pay
// for, and the n-fold cost vector. The bank runs standalone (no Money bound), so cash is its own slot.

#include <limits.h>
#include <gtest/gtest.h>

#include "AoC/AoCCost.h"
#include "AoC/AoCResourceBank.h"
#include "AoC/AoCRules.h"
#include "AoC/AoCWall.h"

namespace
{

enum { FOOD = 1, WOOD = 2, IRON = 3, STEEL = 4 };

class AoCWallTest : public ::testing::Test
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
		m_bank.init(nullptr);
	}

	void TearDown() override
	{
		TheAoCRules = nullptr;
		delete m_rules;
	}

	static AoCCost cost(const char *text)
	{
		AoCCost c;
		AoCCost::parseText(text, c, nullptr);
		return c;
	}

	// The count's definition: n affordable, n + 1 not (unless the request itself is the bound).
	void expectLargestAffordable(const AoCCost &segment, Int requested, Int n)
	{
		EXPECT_EQ(n, AoCWall::affordableCount(m_bank, segment, requested));
		AoCCost total;
		AoCWall::multiply(segment, n, total);
		EXPECT_TRUE(m_bank.canAfford(total));
		if (n < requested)
		{
			AoCWall::multiply(segment, n + 1, total);
			EXPECT_FALSE(m_bank.canAfford(total));
		}
	}

	AoCRules *m_rules;
	AoCResourceBank m_bank;
};

} // namespace

TEST_F(AoCWallTest, ZeroCostAffordsEveryRequestedSegment)
{
	const AoCCost nothing;
	EXPECT_EQ(12, AoCWall::affordableCount(m_bank, nothing, 12));
	EXPECT_EQ(50, AoCWall::affordableCount(m_bank, nothing, 50));
	EXPECT_EQ(1, AoCWall::affordableCount(m_bank, nothing, 1));
	EXPECT_EQ(INT_MAX, AoCWall::affordableCount(m_bank, nothing, INT_MAX));
	// nothing requested, nothing placed
	EXPECT_EQ(0, AoCWall::affordableCount(m_bank, nothing, 0));
	EXPECT_EQ(0, AoCWall::affordableCount(m_bank, nothing, -3));
}

TEST_F(AoCWallTest, ExactMultipleAffordsThatManySegments)
{
	// the gate's numbers: 70 wood, 10 a segment, a 12-segment drag
	m_bank.deposit(WOOD, 70);
	expectLargestAffordable(cost("Wood:10"), 12, 7);
	// 200 wood: the whole drag of 12 (120)
	m_bank.setAmount(WOOD, 200);
	expectLargestAffordable(cost("Wood:10"), 12, 12);
	// a drag shorter than the bank's bound
	m_bank.setAmount(WOOD, 70);
	expectLargestAffordable(cost("Wood:10"), 5, 5);
	expectLargestAffordable(cost("Wood:10"), 7, 7);
}

TEST_F(AoCWallTest, OneShortAffordsOneSegmentLess)
{
	m_bank.deposit(WOOD, 69);
	expectLargestAffordable(cost("Wood:10"), 12, 6);
	// less than one segment: nothing
	m_bank.setAmount(WOOD, 9);
	expectLargestAffordable(cost("Wood:10"), 12, 0);
	m_bank.setAmount(WOOD, 0);
	expectLargestAffordable(cost("Wood:10"), 12, 0);
}

TEST_F(AoCWallTest, TheScarcestComponentBoundsTheLine)
{
	// several types: the smallest quotient wins, cash included, the bank's other types are irrelevant
	m_bank.deposit(WOOD, 100);
	m_bank.deposit(IRON, 12);
	m_bank.deposit(AOC_RESOURCE_CASH, 1000);
	m_bank.deposit(STEEL, 1);
	expectLargestAffordable(cost("Wood:10 Iron:3"), 12, 4);		// iron 12 / 3
	expectLargestAffordable(cost("Wood:10 Cash:250"), 12, 4);		// cash 1000 / 250
	expectLargestAffordable(cost("Wood:15"), 12, 6);					// 100 / 15
	expectLargestAffordable(cost("Cash:100"), 12, 10);					// a cash-only vector counts like any other
	expectLargestAffordable(cost("Food:1"), 12, 0);						// a type the bank lacks
}

TEST_F(AoCWallTest, HugeRequestSaturates)
{
	m_bank.deposit(WOOD, 70);
	expectLargestAffordable(cost("Wood:10"), INT_MAX, 7);
	// the n-fold vector saturates per component instead of wrapping
	AoCCost total;
	AoCWall::multiply(cost("Wood:10 Iron:1"), INT_MAX, total);
	EXPECT_EQ(0xFFFFFFFFu, total.amount[WOOD]);
	EXPECT_EQ((UnsignedInt)INT_MAX, total.amount[IRON]);
	EXPECT_EQ(0u, total.amount[FOOD]);
	EXPECT_FALSE(m_bank.canAfford(total));
	// a full bank: the count is exact (have / cost), although the saturated product of one segment more would pass
	// canAfford against 0xFFFFFFFF
	m_bank.setAmount(WOOD, 0xFFFFFFFFu);
	EXPECT_EQ(429496729, AoCWall::affordableCount(m_bank, cost("Wood:10"), INT_MAX));
	EXPECT_EQ(INT_MAX, AoCWall::affordableCount(m_bank, cost("Wood:1"), INT_MAX));
	m_bank.setAmount(WOOD, 0xFFFFFFFFu);
	EXPECT_EQ(1431655765, AoCWall::affordableCount(m_bank, cost("Wood:3"), INT_MAX));	// 3 x 1431655766 > 0xFFFFFFFF
}

TEST_F(AoCWallTest, NFoldVector)
{
	AoCCost total;
	AoCWall::multiply(cost("Wood:10"), 7, total);
	EXPECT_EQ(cost("Wood:70"), total);
	AoCWall::multiply(cost("Wood:10"), 12, total);
	EXPECT_EQ(cost("Wood:120"), total);
	AoCWall::multiply(cost("Wood:8 Iron:2 Cash:5"), 3, total);
	EXPECT_EQ(cost("Wood:24 Iron:6 Cash:15"), total);
	AoCWall::multiply(cost("Wood:10"), 1, total);
	EXPECT_EQ(cost("Wood:10"), total);
	// zero and negative counts give the zero vector; a zero vector stays zero
	AoCWall::multiply(cost("Wood:10"), 0, total);
	EXPECT_TRUE(total.isZero());
	AoCWall::multiply(cost("Wood:10"), -2, total);
	EXPECT_TRUE(total.isZero());
	AoCWall::multiply(AoCCost(), 12, total);
	EXPECT_TRUE(total.isZero());
	// a product exactly at the limit is kept, one past it saturates
	AoCWall::multiply(cost("Wood:65535"), 65537, total);
	EXPECT_EQ(0xFFFFFFFFu, total.amount[WOOD]);
	AoCWall::multiply(cost("Wood:65536"), 65536, total);
	EXPECT_EQ(0xFFFFFFFFu, total.amount[WOOD]);
}

TEST_F(AoCWallTest, ChargingPlacedSegmentsEmptiesTheBankExactly)
{
	// the logic hook's sequence: count, then one withdrawal per placed segment
	m_bank.deposit(WOOD, 70);
	const AoCCost segment = cost("Wood:10");
	const Int affordable = AoCWall::affordableCount(m_bank, segment, 12);
	ASSERT_EQ(7, affordable);
	for (Int i = 0; i < affordable; ++i)
		EXPECT_TRUE(m_bank.withdraw(segment, FALSE));
	EXPECT_EQ(0u, m_bank.count(WOOD));
	// a segment not placed (units refused to move) is not withdrawn: 7 affordable, 5 placed leaves 20
	m_bank.setAmount(WOOD, 70);
	for (Int i = 0; i < 5; ++i)
		EXPECT_TRUE(m_bank.withdraw(segment, FALSE));
	EXPECT_EQ(20u, m_bank.count(WOOD));
	// 200 wood, 12 placed: 120 charged, 80 left
	m_bank.setAmount(WOOD, 200);
	const Int all = AoCWall::affordableCount(m_bank, segment, 12);
	ASSERT_EQ(12, all);
	for (Int i = 0; i < all; ++i)
		EXPECT_TRUE(m_bank.withdraw(segment, FALSE));
	EXPECT_EQ(80u, m_bank.count(WOOD));
}

TEST_F(AoCWallTest, CostTextNamesTheTypes)
{
	char text[64];
	AoCWall::costText(cost("Wood:70"), text, (Int)sizeof(text));
	EXPECT_STREQ("Wood:70", text);
	AoCWall::costText(cost("Iron:5 Wood:70 Cash:20"), text, (Int)sizeof(text));
	EXPECT_STREQ("Cash:20 Wood:70 Iron:5", text);
	AoCWall::costText(AoCCost(), text, (Int)sizeof(text));
	EXPECT_STREQ("none", text);
}
