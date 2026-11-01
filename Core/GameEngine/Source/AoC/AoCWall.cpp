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

// AoCWall.cpp
// Age of Commanders F10 (design D11 section 2.10): walls with a per-segment charge. See AoCWall.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCWall.h"
#include "AoC/AoCGather.h"
#include "AoC/AoCHarness.h"
#include "AoC/AoCResourceBank.h"

#include <stdio.h>

// ------------------------------------------------------------------------------------------------
void AoCWall::multiply( const AoCCost &segment, Int n, AoCCost &out )
{
	out.clear();
	if (n <= 0)
		return;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
	{
		const UnsignedInt64 total = (UnsignedInt64)segment.amount[i] * (UnsignedInt64)(UnsignedInt)n;
		out.amount[i] = total > 0xFFFFFFFFu ? 0xFFFFFFFFu : (UnsignedInt)total;
	}
}

// ------------------------------------------------------------------------------------------------
Int AoCWall::affordableCount( const AoCResourceBank &bank, const AoCCost &segment, Int requested )
{
	if (requested <= 0)
		return 0;
	// n segments are affordable when every component holds have >= cost * n, that is n <= have / cost in whole
	// numbers; the smallest quotient over the non-zero components bounds the line. A zero vector bounds nothing.
	UnsignedInt best = (UnsignedInt)requested;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
	{
		if (segment.amount[i] == 0)
			continue;
		const UnsignedInt n = bank.count(i) / segment.amount[i];
		if (n < best)
			best = n;
	}
	return (Int)best;
}

// ------------------------------------------------------------------------------------------------
// Harness lines
// ------------------------------------------------------------------------------------------------
namespace
{
	struct PreviewLogState
	{
		UnsignedInt lastFrame;
		Int tiles;
		Int affordable;
		Bool any;
	};

	PreviewLogState &previewLog()
	{
		static PreviewLogState s = { 0u, -1, -1, FALSE };	// fixed storage, nothing allocated
		return s;
	}
}

// ------------------------------------------------------------------------------------------------
Bool AoCWall::logEnabled()
{
	return AoCHarness::isActive();
}

// ------------------------------------------------------------------------------------------------
void AoCWall::costText( const AoCCost &cost, char *out, Int outSize )
{
	if (out == nullptr || outSize <= 0)
		return;
	out[0] = '\0';
	Int used = 0;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
	{
		if (cost.amount[i] == 0)
			continue;
		const int n = snprintf(out + used, (size_t)(outSize - used), "%s%s:%u", used > 0 ? " " : "", AoCGather::typeName(i), cost.amount[i]);
		if (n < 0 || used + n >= outSize)
			break;
		used += n;
	}
	if (used == 0)
		snprintf(out, (size_t)outSize, "none");
}

// ------------------------------------------------------------------------------------------------
void AoCWall::logLine( UnsignedInt frame, Int playerIndex, Int requested, Int affordable, Int placed, const AoCCost &charged )
{
	char text[256];
	costText(charged, text, (Int)sizeof(text));
	fprintf(stderr, "[AoC wall] frame %u player %d line requested %d affordable %d placed %d cost %s\n", frame, playerIndex, requested,
		affordable, placed, text);
	fflush(stderr);
}

// ------------------------------------------------------------------------------------------------
void AoCWall::logPreview( UnsignedInt clientFrame, Int tiles, Int affordable )
{
	PreviewLogState &s = previewLog();
	// a gap of more than one client frame since the last preview frame starts a new drag: print its first pair again
	const Bool newDrag = !s.any || (clientFrame != s.lastFrame && clientFrame != s.lastFrame + 1);
	const Bool changed = newDrag || tiles != s.tiles || affordable != s.affordable;
	s.any = TRUE;
	s.lastFrame = clientFrame;
	s.tiles = tiles;
	s.affordable = affordable;
	if (!changed)
		return;
	fprintf(stderr, "[AoC wall] preview %d affordable %d\n", tiles, affordable);
	fflush(stderr);
}
