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
// table, the point picker, the pause draw, the update's transitions and the runtime state with its save block and CRC.
//
//   Behavior = AoCWanderAIUpdate ModuleTag_x    ; replaces the template's AI module; repeat its Locomotor lines after it
//     WanderRadius           = 60     ; world units (default 60)
//     MinPause               = 2000   ; milliseconds between two moves, at least (default 2000)
//     MaxPause               = 8000   ; milliseconds, at most (default 8000); also the watchdog's patience
//     AnchorToProducer       = Yes    ; stroll around the object that made it (SpawnBehavior's producer), else around
//                                     ; the spot where it stood at its first update (default Yes)
//     ClearProducerFootprint = Yes    ; anchored to a producer, count the radius from its footprint (default Yes); No
//   End                               ; anchors the ring on the producer's centre (an ox pen built as one structure)
//
// The anchor is fixed at the first update. Anchored to a producer with ClearProducerFootprint, the radius counts from
// the producer's footprint, its bounding circle: a point is picked at a distance from clearance to clearance +
// WanderRadius of the producer's centre, so it never falls inside the building that spawned the walker (a town
// centre's footprint holds the whole 60-unit circle around its centre). Otherwise the clearance is 0. Each pick is a
// direction (one of 64, from a fixed table: no trigonometry in logic, D11 1.2) and a distance, both from
// GameLogicRandomValue, so the point is a pure function of the two draws.
//
// The update, in order (AoCWanderAIUpdate::update, each step a function below):
//   1. watchStuck: a walker whose stock AI sits in a move (AI_MOVE_TO) without getting anywhere for MaxPause frames,
//      while it has a stroll in flight or its pause is over, is stuck (two units locked on one spot by the stock
//      collision code); the module sends the stock AI idle and picks again at once.
//   2. wantsMove: the pause is over and the stock AI is idle: pick and walk (moveStarted), or pause again when no
//      draw gives a usable destination (pauseFor).
//   3. the stock AI update.
//   4. moveEnded: the stock AI is idle again with a stroll in flight: pause (pauseFor).
//   5. sleepFrames: the rest of the pause, else a quarter of MinPause; the stock AI's own request wins when sooner.

#pragma once

#include "Lib/BaseType.h"

struct FieldParse;
class Xfer;

enum
{
	AOC_WANDER_DIRECTIONS = 64,				///< directions the picker chooses from, 5.625 degrees apart
	AOC_WANDER_PICK_ATTEMPTS = 4,			///< draws per move before the walker gives up and pauses again
	AOC_WANDER_XFER_VERSION = 1,			///< the module's save block version
};

/// A walker that moved less than this (world units) since the watch began has made no progress (the watchdog).
#define AOC_WANDER_PROGRESS_DISTANCE (1.0f)

/// Module data of AoCWanderAIUpdate (D11 2.9 keys and defaults). The durations are frames, converted from milliseconds
/// at parse time by INI::parseDurationUnsignedInt at the live simulation rate (F15), as every stock duration is.
struct AoCWanderData
{
	Real wanderRadius;					///< WanderRadius, world units
	UnsignedInt minPauseFrames;	///< MinPause
	UnsignedInt maxPauseFrames;	///< MaxPause; also how long a walker may sit in a stock move without progress
	Bool anchorToProducer;			///< AnchorToProducer
	Bool clearProducerFootprint;	///< ClearProducerFootprint

	AoCWanderData();

	/// The parse table, offsets relative to this struct (the module data adds it with the member's offset).
	static const FieldParse *getFieldParse();
};

/// Runtime state of one walker: the module's save block (version 1) and its CRC.
struct AoCWanderState
{
	Bool anchored;							///< the first update has fixed the anchor
	Coord3D anchor;							///< the producer's position, else the walker's own at its first update
	Real clearance;							///< the producer's bounding circle radius, 0 when anchored to itself or not clearing
	UnsignedInt nextMoveFrame;	///< the earliest logic frame of the next move (the end of the pause)
	Bool moving;								///< a wander move is in flight (issued and not yet ended)
	Coord3D dest;								///< the destination of the last move issued (the harness prints its distance)
	Coord3D watchPos;						///< the watchdog: where the walker stood when the watch began
	UnsignedInt watchFrame;			///< the watchdog: the frame the watch began

	AoCWanderState();

	void crc( Xfer *xfer );
	/// The five-plus-three fields, in save order.
	void xfer( Xfer *xfer );

	/// The stock AI block of the module, written between the version byte and the fields.
	typedef void (*StockBlock)( Xfer *xfer, void *context );
	/// The module's whole save block: the version byte (AOC_WANDER_XFER_VERSION), the stock AI block, then the fields.
	/// AoCWanderAIUpdate::xfer is this call with AIUpdateInterface::xfer as the stock block.
	void xferBlock( Xfer *xfer, StockBlock stock, void *context );
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

	/// The clearance of an anchor: the producer's bounding circle radius when anchored to a producer that clears its
	/// footprint, else 0.
	Real anchorClearance( Bool anchoredToProducer, Bool clearFootprint, Real producerBoundingRadius );

	/// The farthest a pick can lie from the anchor: clearance + radius.
	inline Real outerRadius( Real clearance, Real radius ) { return clearance + radius; }

	/// TRUE when pos is within reach + slack of the anchor in 2D (squares compared: no square root in logic). The module
	/// accepts a destination the pathfinder adjusted only within one cell of the outer radius.
	Bool withinReach( const Coord3D &anchor, Real reach, Real slack, const Coord3D &pos );

	// --- the update's transitions (steps 1, 2, 4 and 5 above) ---

	/// The anchor is fixed (first update): anchor, clearance, the first move due now, the watch starts here.
	void fixAnchor( AoCWanderState &s, UnsignedInt now, const Coord3D &anchor, Real clearance, const Coord3D &pos );

	/// Step 1, the watchdog. Restarts the watch (pos, now) whenever the stock AI is not in a move (inStockMove FALSE) or
	/// the walker has moved more than AOC_WANDER_PROGRESS_DISTANCE since it began. TRUE (stuck) when the walker has sat in
	/// a stock move without that progress for stuckFrames frames while a stroll is in flight or its pause is over; the
	/// state is then ready for a fresh pick at once (no stroll in flight, the next move due now, the watch restarted).
	/// The caller sends the stock AI idle.
	Bool watchStuck( AoCWanderState &s, UnsignedInt now, Bool inStockMove, const Coord3D &pos, UnsignedInt stuckFrames );

	/// Step 2: TRUE when a stroll should start: none in flight, the pause over, the stock AI idle, the walker alive.
	Bool wantsMove( const AoCWanderState &s, UnsignedInt now, Bool idle, Bool dead );

	/// A stroll to dest was issued: in flight, the watch restarted.
	void moveStarted( AoCWanderState &s, UnsignedInt now, const Coord3D &pos, const Coord3D &dest );

	/// Step 4: TRUE when the stroll in flight has ended (the stock AI is idle again).
	Bool moveEnded( const AoCWanderState &s, Bool idle );

	/// A pause of pauseFrames starts now (after a stroll ended, or when no draw gave a destination): nothing in flight,
	/// the next move due at now + pauseFrames.
	void pauseFor( AoCWanderState &s, UnsignedInt now, UnsignedInt pauseFrames );

	/// Step 5: the module's own sleep: the rest of the pause while pausing, else checkFrames(minPauseFrames).
	UnsignedInt sleepFrames( const AoCWanderState &s, UnsignedInt now, UnsignedInt minPauseFrames );

	// --- harness ---

	/// The 2D distance from the anchor, rounded to the nearest whole unit. For the harness line only, never for logic.
	Int distanceForLog( const Coord3D &anchor, const Coord3D &pos );

	/// Harness evidence (any harness flag given); nothing in a stock run; logic never reads it:
	///   "[AoC wander] frame N <objectId> dist=<d> dest=<d> pause=<frames> max=<outer>"  a stroll ended (dist: where the
	///       walker stands, dest: the destination issued, both from the anchor; max: clearance + WanderRadius, rounded up)
	///   "[AoC wander] frame N <objectId> pick failed pause=<frames> max=<outer>"         no draw gave a usable destination
	///   "[AoC wander] frame N <objectId> unstuck dist=<d> max=<outer>"                    the watchdog took over
	Bool logEnabled();
	void logArrival( UnsignedInt objectId, Int dist, Int destDist, UnsignedInt pauseFrames, Int maxDist );
	void logPickFailed( UnsignedInt objectId, UnsignedInt pauseFrames, Int maxDist );
	void logUnstuck( UnsignedInt objectId, Int dist, Int maxDist );
}
