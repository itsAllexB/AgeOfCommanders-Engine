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

// AoC F2b: resource nodes as mouse targets (ticket F2b decision 1, design D11 section 2.2). The pick-type decision:
// the second pick with PICK_TYPE_SHRUBBERY happens only under the AoC rules, outside every mode and GUI command that
// decides a click by itself, for a controllable selection holding a gatherer, and only when the stock types do not pick
// trees already; with the rules off it never happens, whatever else holds, so the stock pick stands bit for bit.

#include <gtest/gtest.h>

#include "AoC/AoCNodePick.h"
#include "GameClient/View.h"

namespace
{

/// The one context that asks for the second pick: rules on, no mode, no GUI command, a controllable selection with a
/// gatherer, the stock click types (selectable only).
AoCNodePickContext plainGathererClick()
{
	AoCNodePickContext c;
	c.rulesOn = TRUE;
	c.controllable = TRUE;
	c.hasGatherer = TRUE;
	c.stockTypes = PICK_TYPE_SELECTABLE;
	return c;
}

/// Context number 'bits' of the 2^8 combinations of the eight flags, with the given stock types.
AoCNodePickContext combination( UnsignedInt bits, UnsignedInt stockTypes )
{
	AoCNodePickContext c;
	c.rulesOn = (bits & 1u) != 0;
	c.forceAttack = (bits & 2u) != 0;
	c.forceMove = (bits & 4u) != 0;
	c.waypoint = (bits & 8u) != 0;
	c.attackMove = (bits & 16u) != 0;
	c.guiCommand = (bits & 32u) != 0;
	c.controllable = (bits & 64u) != 0;
	c.hasGatherer = (bits & 128u) != 0;
	c.stockTypes = stockTypes;
	return c;
}

} // namespace

// A worker selected, a plain click or mouse-over: the trees are added, and nothing else.
TEST(AoCNodePickTest, GathererSelectionAddsShrubberyOnly)
{
	EXPECT_EQ((UnsignedInt)PICK_TYPE_SHRUBBERY, AoCNodePick::extraPickTypes(plainGathererClick()));

	// the mouse-over's stock types (getPickTypesForContext(true): selectable and force-attackable) change nothing
	AoCNodePickContext hover = plainGathererClick();
	hover.stockTypes = PICK_TYPE_SELECTABLE | PICK_TYPE_FORCEATTACKABLE;
	EXPECT_EQ((UnsignedInt)PICK_TYPE_SHRUBBERY, AoCNodePick::extraPickTypes(hover));

	// a mine-allowing pick neither
	hover.stockTypes = PICK_TYPE_SELECTABLE | PICK_TYPE_MINES;
	EXPECT_EQ((UnsignedInt)PICK_TYPE_SHRUBBERY, AoCNodePick::extraPickTypes(hover));
}

// Stock data: no AoCResourceType, no second pick, for every combination of the other flags and stock types.
TEST(AoCNodePickTest, RulesOffNeverAddsAType)
{
	const UnsignedInt stockTypes[] = { 0u, (UnsignedInt)PICK_TYPE_SELECTABLE, (UnsignedInt)(PICK_TYPE_SELECTABLE | PICK_TYPE_FORCEATTACKABLE),
		(UnsignedInt)PICK_TYPE_ALL_DRAWABLES };
	for (size_t t = 0; t < sizeof(stockTypes) / sizeof(stockTypes[0]); ++t)
	{
		for (UnsignedInt bits = 0; bits < 256u; bits += 2u)	// bit 0 (rulesOn) clear
		{
			const AoCNodePickContext c = combination(bits, stockTypes[t]);
			ASSERT_FALSE(c.rulesOn);
			EXPECT_EQ(0u, AoCNodePick::extraPickTypes(c)) << "flags " << bits << " stock types " << stockTypes[t];
		}
	}
}

// Each mode and the GUI command keep the stock pick: force attack fires at the tree, force move and waypoints go to the
// ground, attack move is its own command, a GUI command picks by its ALLOW_SHRUBBERY_TARGET option.
TEST(AoCNodePickTest, EveryModeAndGuiCommandKeepTheStockPick)
{
	AoCNodePickContext c = plainGathererClick();
	c.forceAttack = TRUE;
	EXPECT_EQ(0u, AoCNodePick::extraPickTypes(c));
	c = plainGathererClick();
	c.forceMove = TRUE;
	EXPECT_EQ(0u, AoCNodePick::extraPickTypes(c));
	c = plainGathererClick();
	c.waypoint = TRUE;
	EXPECT_EQ(0u, AoCNodePick::extraPickTypes(c));
	c = plainGathererClick();
	c.attackMove = TRUE;
	EXPECT_EQ(0u, AoCNodePick::extraPickTypes(c));
	c = plainGathererClick();
	c.guiCommand = TRUE;
	EXPECT_EQ(0u, AoCNodePick::extraPickTypes(c));
}

// A dozer, a tank or an enemy's unit selected: no gatherer or not controllable, so no second pick (the move order and
// the move cursor over a tree stay the stock ones).
TEST(AoCNodePickTest, NoGathererOrNotControllableKeepsTheStockPick)
{
	AoCNodePickContext c = plainGathererClick();
	c.hasGatherer = FALSE;
	EXPECT_EQ(0u, AoCNodePick::extraPickTypes(c));
	c = plainGathererClick();
	c.controllable = FALSE;
	EXPECT_EQ(0u, AoCNodePick::extraPickTypes(c));
	c = plainGathererClick();
	c.controllable = FALSE;
	c.hasGatherer = FALSE;
	EXPECT_EQ(0u, AoCNodePick::extraPickTypes(c));
}

// The stock types already pick trees (a flame weapon in the mouse-over pick): the stock pick decides alone.
TEST(AoCNodePickTest, StockTypesWithShrubberyNeedNoSecondPick)
{
	AoCNodePickContext c = plainGathererClick();
	c.stockTypes = PICK_TYPE_SELECTABLE | PICK_TYPE_SHRUBBERY;
	EXPECT_EQ(0u, AoCNodePick::extraPickTypes(c));
	c.stockTypes = PICK_TYPE_ALL_DRAWABLES;
	EXPECT_EQ(0u, AoCNodePick::extraPickTypes(c));
}

// Over all 256 combinations of the flags: the second pick happens for exactly one, the plain gatherer click.
TEST(AoCNodePickTest, ExactlyThePlainGathererContextAsksForTrees)
{
	Int asking = 0;
	for (UnsignedInt bits = 0; bits < 256u; ++bits)
	{
		const AoCNodePickContext c = combination(bits, PICK_TYPE_SELECTABLE);
		const UnsignedInt extra = AoCNodePick::extraPickTypes(c);
		const Bool expected = c.rulesOn && !c.forceAttack && !c.forceMove && !c.waypoint && !c.attackMove && !c.guiCommand &&
			c.controllable && c.hasGatherer;
		EXPECT_EQ(expected ? (UnsignedInt)PICK_TYPE_SHRUBBERY : 0u, extra) << "flags " << bits;
		asking += extra != 0 ? 1 : 0;
	}
	EXPECT_EQ(1, asking);
}

// The default context (as constructed) is the stock game: nothing on, nothing added.
TEST(AoCNodePickTest, DefaultContextIsStock)
{
	const AoCNodePickContext c;
	EXPECT_FALSE(c.rulesOn);
	EXPECT_FALSE(c.hasGatherer);
	EXPECT_EQ(0u, c.stockTypes);
	EXPECT_EQ(0u, AoCNodePick::extraPickTypes(c));
}
