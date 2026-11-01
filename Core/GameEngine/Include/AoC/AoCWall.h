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

// AoCWall.h
// Age of Commanders F10 (design D11 section 2.10, D4 question 5, D10): walls with a per-segment charge.
//
// Stock drags a KINDOF_LINEBUILD template as a line (MSG_DOZER_CONSTRUCT_LINE): BuildAssistant::buildObjectLineNow
// tiles the positions from the start to the end (at most GameData.ini MaxLineBuildObjects, stock 50) and calls
// buildObjectNow per segment, whose instant branch creates each segment and charges nothing. Under the AoC rules
// (resource types defined, AoCRules::enabled) buildObjectLineNow now:
//   1. takes the segment's cost vector, ThingTemplate::calcAoCCost (cash = the stock BuildCost with its modifiers);
//   2. cuts the line to affordableCount(bank, segment, tiles): the largest n the player's bank can pay n segments of;
//   3. places those segments with the stock buildObjectNow and withdraws one segment's vector after each one that
//      returns an object (for a human player buildObjectNow returns null when units in the way refuse to move; such
//      a segment is not charged).
// With the rules off (stock data) the stock loop runs and the bank is not read. buildObjectNow itself is untouched:
// its instant branch is also the solo AI's and the map scripts' "out of thin air" path and stays free. Charges are
// immediate, so nothing new is saved or enters the CRC (the bank already does both, F1).
//
// The client's line preview (InGameUI::handleBuildPlacements) tints every segment beyond the same count, read from the
// local player's bank, with the stock IllegalBuildColor; it only reads. The logic recomputes the count when the
// message arrives, so a replay or a LAN peer gets the same result whatever a client showed.

#pragma once

#include "AoC/AoCCost.h"

class AoCResourceBank;

namespace AoCWall
{
	/// out = segment x n, each component saturating at 0xFFFFFFFF; n <= 0 gives the zero vector.
	void multiply( const AoCCost &segment, Int n, AoCCost &out );

	/// The largest n in [0, requested] with bank.canAfford(segment x n): the segments the bank can pay for. Exact for
	/// any amounts: per component, n <= have / cost (integer division), so no product is formed and nothing overflows.
	/// A zero vector affords every requested segment; requested <= 0 gives 0.
	Int affordableCount( const AoCResourceBank &bank, const AoCCost &segment, Int requested );

	// --- harness evidence (any harness flag given; the callers run only under the AoC rules); logic never reads it ---
	//   "[AoC wall] frame <f> player <n> line requested <tiles> affordable <n> placed <k> cost <vector>"
	//        logic, per line message: the tiles the stock tiling found, the segments the bank could pay for, the
	//        segments placed, and the vector charged (segment x placed; "none" when nothing was charged)
	//   "[AoC wall] preview <tiles> affordable <n>"
	//        client, the line preview: printed when the pair changes, and at the first preview frame of a drag
	Bool logEnabled();
	void logLine( UnsignedInt frame, Int playerIndex, Int requested, Int affordable, Int placed, const AoCCost &charged );
	void logPreview( UnsignedInt clientFrame, Int tiles, Int affordable );
	/// "Wood:70 Iron:5" (the defined type names, zero components left out), "none" for the zero vector.
	void costText( const AoCCost &cost, char *out, Int outSize );
}
