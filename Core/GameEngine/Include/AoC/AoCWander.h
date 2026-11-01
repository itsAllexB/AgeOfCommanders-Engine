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

// AoCWander.h
// Age of Commanders F9 (design D11 section 2.9): bounded wander for ambient civilians and oxen (D3 section 6,
// D5 section 6). The pure parts of AoCWanderAIUpdate, so the unit tests can run them: the module data and its parse
// table, the point picker, the pause draw and the runtime state with its save and CRC.
//
//   Behavior = AoCWanderAIUpdate ModuleTag_x    ; replaces the template's AI module; repeat its Locomotor lines after it
//     WanderRadius     = 60     ; world units (default 60)
//     MinPause         = 2000   ; milliseconds between two moves, at least (default 2000)
//     MaxPause         = 8000   ; milliseconds, at most (default 8000)
//     AnchorToProducer = Yes    ; stroll around the object that made it (SpawnBehavior's producer), else around the
//   End                         ; spot where it stood at its first update (default Yes)
//
// The anchor is fixed at the first update. Anchored to a producer, the radius counts from the producer's footprint,
// its bounding circle: a point is picked at a distance from clearance to clearance + WanderRadius of the producer's
// centre, so it never falls inside the building that spawned the walker (a town centre's footprint holds the whole
// 60-unit circle around its centre). Anchored to itself, the clearance is 0. Each pick is a direction (one of 64,
// from a fixed table: no trigonometry in logic, D11 1.2) and a distance, both from GameLogicRandomValue, so the point
// is a pure function of the two draws.

#pragma once

#include "Lib/BaseType.h"

struct FieldParse;
class Xfer;

enum
{
	AOC_WANDER_DIRECTIONS = 64,				///< directions the picker chooses from, 5.625 degrees apart
	AOC_WANDER_PICK_ATTEMPTS = 4,			///< draws per move before the walker gives up and pauses again
};

/// Module data of AoCWanderAIUpdate (D11 2.9 keys and defaults). The durations are frames, converted from milliseconds
/// at parse time by INI::parseDurationUnsignedInt at the live simulation rate (F15), as every stock duration is.
struct AoCWanderData
{
	Real wanderRadius;					///< WanderRadius, world units
	UnsignedInt minPauseFrames;	///< MinPause
	UnsignedInt maxPauseFrames;	///< MaxPause
	Bool anchorToProducer;			///< AnchorToProducer

	AoCWanderData();

	/// The parse table, offsets relative to this struct (the module data adds it with the member's offset).
	static const FieldParse *getFieldParse();
};

/// Runtime state of one walker: saved (xfer version 1 of the module) and in the module's CRC.
struct AoCWanderState
{
	Bool anchored;							///< the first update has fixed the anchor
	Coord3D anchor;							///< the producer's position, else the walker's own at its first update
	Real clearance;							///< the producer's bounding circle radius, 0 when anchored to itself
	UnsignedInt nextMoveFrame;	///< the earliest logic frame of the next move (the end of the pause)
	Bool moving;								///< a wander move is in flight (issued and not yet ended)

	AoCWanderState();

	void crc( Xfer *xfer );
	void xfer( Xfer *xfer );
};

/// Source of the draws, so the unit tests can inject their own. The module's draws GameLogicRandomValue and
/// GameLogicRandomValueReal; nothing else is drawn.
class AoCWanderRandom
{
public:
	virtual ~AoCWanderRandom() {}
	virtual Int randomInt( Int lo, Int hi ) = 0;			///< lo to hi inclusive
	virtual Real randomReal( Real lo, Real hi ) = 0;	///< lo to hi
};

namespace AoCWander
{
	/// Unit vector of direction d (0 to AOC_WANDER_DIRECTIONS - 1, taken modulo), counter-clockwise from +x.
	void direction( Int d, Real *x, Real *y );

	/// The pick as a pure function of the draws: anchor + (clearance + distance) * direction(directionDraw), z from the
	/// anchor. distanceDraw is clamped to 0 to radius, so the point lies between clearance and clearance + radius from
	/// the anchor by construction.
	Coord3D pickPoint( const Coord3D &anchor, Real clearance, Real radius, Int directionDraw, Real distanceDraw );

	/// One pick from the random source: a direction, then a distance (always two draws, in that order).
	Coord3D drawPoint( const Coord3D &anchor, Real clearance, Real radius, AoCWanderRandom &random );

	/// The pause after a move, minFrames to maxFrames inclusive (one draw; a maximum below the minimum is the minimum),
	/// never below one frame.
	UnsignedInt drawPause( UnsignedInt minFrames, UnsignedInt maxFrames, AoCWanderRandom &random );

	/// How long a walker with a move in flight (or waiting for its stock AI to go idle) sleeps between two looks:
	/// a quarter of MinPause, at least one frame (D11 F9 decision 1).
	UnsignedInt checkFrames( UnsignedInt minPauseFrames );

	/// The farthest a pick can lie from the anchor: clearance + radius.
	inline Real outerRadius( Real clearance, Real radius ) { return clearance + radius; }

	/// TRUE when pos is within reach + slack of the anchor in 2D (squares compared: no square root in logic). The module
	/// accepts a destination the pathfinder adjusted only within one cell of the outer radius.
	Bool withinReach( const Coord3D &anchor, Real reach, Real slack, const Coord3D &pos );

	/// The 2D distance from the anchor, rounded to the nearest whole unit. For the harness line only, never for logic.
	Int distanceForLog( const Coord3D &anchor, const Coord3D &pos );

	/// Harness evidence (any harness flag given): "[AoC wander] frame N <objectId> dist=<d> pause=<frames> max=<outer>",
	/// one line per ended move. Nothing in a stock run; logic never reads it.
	Bool logEnabled();
	void logArrival( UnsignedInt objectId, Int dist, UnsignedInt pauseFrames, Int maxDist );
}
