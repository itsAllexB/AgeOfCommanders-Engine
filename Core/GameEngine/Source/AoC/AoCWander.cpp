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

// AoCWander.cpp
// Age of Commanders F9: bounded wander, the pure parts of AoCWanderAIUpdate. See AoCWander.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCWander.h"
#include "AoC/AoCHarness.h"
#include "Common/GameCommon.h"
#include "Common/INI.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "WWMath/wwmath.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

namespace
{
	// cos(k * pi / 32) for k = 0 to 16: a quarter turn in 16 steps of 5.625 degrees. Literal constants, so every
	// compiler and platform starts from the same floats (no trigonometry in logic, D11 1.2).
	const Real QUARTER_COS[17] =
	{
		1.000000000f, 0.995184727f, 0.980785280f, 0.956940336f, 0.923879533f, 0.881921264f, 0.831469612f, 0.773010453f,
		0.707106781f, 0.634393284f, 0.555570233f, 0.471396737f, 0.382683432f, 0.290284677f, 0.195090322f, 0.098017140f,
		0.000000000f
	};

	// The INI duration rule (INI::parseDurationUnsignedInt): the ceiling of ms * LOGICFRAMES_PER_MSEC_REAL at the live rate.
	UnsignedInt msecsToFrames( UnsignedInt msecs )
	{
		return (UnsignedInt)WWMath::Ceil(ConvertDurationFromMsecsToFrames((Real)msecs));
	}
}

// ------------------------------------------------------------------------------------------------
AoCWanderData::AoCWanderData()
{
	wanderRadius = 60.0f;
	minPauseFrames = msecsToFrames(2000);
	maxPauseFrames = msecsToFrames(8000);
	anchorToProducer = TRUE;
	clearProducerFootprint = TRUE;
}

// ------------------------------------------------------------------------------------------------
/*static*/ const FieldParse *AoCWanderData::getFieldParse()
{
	static const FieldParse dataFieldParse[] =
	{
		{ "WanderRadius",			INI::parseReal,									nullptr, offsetof( AoCWanderData, wanderRadius ) },
		{ "MinPause",					INI::parseDurationUnsignedInt,	nullptr, offsetof( AoCWanderData, minPauseFrames ) },
		{ "MaxPause",					INI::parseDurationUnsignedInt,	nullptr, offsetof( AoCWanderData, maxPauseFrames ) },
		{ "AnchorToProducer",	INI::parseBool,									nullptr, offsetof( AoCWanderData, anchorToProducer ) },
		{ "ClearProducerFootprint", INI::parseBool,						nullptr, offsetof( AoCWanderData, clearProducerFootprint ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return dataFieldParse;
}

// ------------------------------------------------------------------------------------------------
AoCWanderState::AoCWanderState()
{
	anchored = FALSE;
	anchor.zero();
	clearance = 0.0f;
	nextMoveFrame = 0;
	moving = FALSE;
	dest.zero();
	watchPos.zero();
	watchFrame = 0;
}

// ------------------------------------------------------------------------------------------------
void AoCWanderState::crc( Xfer *xfer )
{
	AoCWanderState::xfer(xfer);
}

// ------------------------------------------------------------------------------------------------
void AoCWanderState::xfer( Xfer *xfer )
{
	xfer->xferBool(&anchored);
	xfer->xferCoord3D(&anchor);
	xfer->xferReal(&clearance);
	xfer->xferUnsignedInt(&nextMoveFrame);
	xfer->xferBool(&moving);
	xfer->xferCoord3D(&dest);
	xfer->xferCoord3D(&watchPos);
	xfer->xferUnsignedInt(&watchFrame);
}

// ------------------------------------------------------------------------------------------------
/** The module's save block.
	* Version Info:
	* 1: Initial version: the stock AI block, then anchored, anchor, clearance, next move frame, move in flight,
	*    destination, watch position, watch frame */
// ------------------------------------------------------------------------------------------------
void AoCWanderState::xferBlock( Xfer *xfer, StockBlock stock, void *context )
{
	XferVersion currentVersion = AOC_WANDER_XFER_VERSION;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	if (stock)
		stock(xfer, context);
	AoCWanderState::xfer(xfer);
}

// ------------------------------------------------------------------------------------------------
void AoCWander::direction( Int d, Real *x, Real *y )
{
	d %= AOC_WANDER_DIRECTIONS;
	if (d < 0)
		d += AOC_WANDER_DIRECTIONS;
	const Int quadrant = d / 16;
	const Int k = d % 16;
	const Real c = QUARTER_COS[k];
	const Real s = QUARTER_COS[16 - k];
	switch (quadrant)
	{
		case 0:		*x = c;		*y = s;		break;
		case 1:		*x = -s;	*y = c;		break;
		case 2:		*x = -c;	*y = -s;	break;
		default:	*x = s;		*y = -c;	break;
	}
}

// ------------------------------------------------------------------------------------------------
Coord3D AoCWander::pickPoint( const Coord3D &anchor, Real clearance, Real radius, Int directionDraw, Real distanceDraw )
{
	if (radius < 0.0f)
		radius = 0.0f;
	if (clearance < 0.0f)
		clearance = 0.0f;
	if (distanceDraw < 0.0f)
		distanceDraw = 0.0f;
	if (distanceDraw > radius)
		distanceDraw = radius;

	Real dx, dy;
	direction(directionDraw, &dx, &dy);
	const Real distance = clearance + distanceDraw;
	Coord3D point = anchor;
	point.x += distance * dx;
	point.y += distance * dy;
	return point;
}

// ------------------------------------------------------------------------------------------------
Coord3D AoCWander::drawPoint( const Coord3D &anchor, Real clearance, Real radius, AoCWanderRandom &random )
{
	const Int directionDraw = random.randomInt(0, AOC_WANDER_DIRECTIONS - 1);
	const Real distanceDraw = random.randomReal(0.0f, radius > 0.0f ? radius : 0.0f);
	return pickPoint(anchor, clearance, radius, directionDraw, distanceDraw);
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCWander::drawPause( UnsignedInt minFrames, UnsignedInt maxFrames, AoCWanderRandom &random )
{
	if (maxFrames < minFrames)
		maxFrames = minFrames;
	const Int pause = random.randomInt((Int)minFrames, (Int)maxFrames);
	return pause < 1 ? 1u : (UnsignedInt)pause;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCWander::checkFrames( UnsignedInt minPauseFrames )
{
	const UnsignedInt quarter = minPauseFrames / 4;
	return quarter < 1 ? 1u : quarter;
}

// ------------------------------------------------------------------------------------------------
Real AoCWander::anchorClearance( Bool anchoredToProducer, Bool clearFootprint, Real producerBoundingRadius )
{
	if (!anchoredToProducer || !clearFootprint || producerBoundingRadius < 0.0f)
		return 0.0f;
	return producerBoundingRadius;
}

// ------------------------------------------------------------------------------------------------
Bool AoCWander::withinReach( const Coord3D &anchor, Real reach, Real slack, const Coord3D &pos )
{
	const Real dx = pos.x - anchor.x;
	const Real dy = pos.y - anchor.y;
	const Real limit = reach + slack;
	return dx * dx + dy * dy <= limit * limit;
}

// ------------------------------------------------------------------------------------------------
void AoCWander::fixAnchor( AoCWanderState &s, UnsignedInt now, const Coord3D &anchor, Real clearance, const Coord3D &pos )
{
	s.anchored = TRUE;
	s.anchor = anchor;
	s.clearance = clearance;
	s.nextMoveFrame = now;
	s.moving = FALSE;
	s.watchPos = pos;
	s.watchFrame = now;
}

// ------------------------------------------------------------------------------------------------
Bool AoCWander::watchStuck( AoCWanderState &s, UnsignedInt now, Bool inStockMove, const Coord3D &pos, UnsignedInt stuckFrames )
{
	const Real dx = pos.x - s.watchPos.x;
	const Real dy = pos.y - s.watchPos.y;
	const Real progress = AOC_WANDER_PROGRESS_DISTANCE;
	if (!inStockMove || dx * dx + dy * dy > progress * progress || now < s.watchFrame)
	{
		s.watchPos = pos;
		s.watchFrame = now;
		return FALSE;
	}
	if (!s.moving && now < s.nextMoveFrame)
		return FALSE;		// pausing: a stock move then is not the module's business yet
	if (now - s.watchFrame < (stuckFrames < 1 ? 1u : stuckFrames))
		return FALSE;
	s.moving = FALSE;
	s.nextMoveFrame = now;
	s.watchPos = pos;
	s.watchFrame = now;
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
Bool AoCWander::wantsMove( const AoCWanderState &s, UnsignedInt now, Bool idle, Bool dead )
{
	return !s.moving && now >= s.nextMoveFrame && idle && !dead;
}

// ------------------------------------------------------------------------------------------------
void AoCWander::moveStarted( AoCWanderState &s, UnsignedInt now, const Coord3D &pos, const Coord3D &dest )
{
	s.moving = TRUE;
	s.dest = dest;
	s.watchPos = pos;
	s.watchFrame = now;
}

// ------------------------------------------------------------------------------------------------
Bool AoCWander::moveEnded( const AoCWanderState &s, Bool idle )
{
	return s.moving && idle;
}

// ------------------------------------------------------------------------------------------------
void AoCWander::pauseFor( AoCWanderState &s, UnsignedInt now, UnsignedInt pauseFrames )
{
	s.moving = FALSE;
	s.nextMoveFrame = now + pauseFrames;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCWander::sleepFrames( const AoCWanderState &s, UnsignedInt now, UnsignedInt minPauseFrames )
{
	if (!s.moving && now < s.nextMoveFrame)
		return s.nextMoveFrame - now;
	return checkFrames(minPauseFrames);
}

// ------------------------------------------------------------------------------------------------
Int AoCWander::distanceForLog( const Coord3D &anchor, const Coord3D &pos )
{
	const double dx = (double)pos.x - (double)anchor.x;
	const double dy = (double)pos.y - (double)anchor.y;
	return (Int)floor(sqrt(dx * dx + dy * dy) + 0.5);
}

// ------------------------------------------------------------------------------------------------
Bool AoCWander::logEnabled()
{
	return AoCHarness::isActive();
}

// ------------------------------------------------------------------------------------------------
void AoCWander::logArrival( UnsignedInt objectId, Int dist, Int destDist, UnsignedInt pauseFrames, Int maxDist )
{
	if (!logEnabled())
		return;
	fprintf(stderr, "[AoC wander] frame %u %u dist=%d dest=%d pause=%u max=%d\n", TheGameLogic ? TheGameLogic->getFrame() : 0u,
		objectId, (int)dist, (int)destDist, pauseFrames, (int)maxDist);
	fflush(stderr);
}

// ------------------------------------------------------------------------------------------------
void AoCWander::logPickFailed( UnsignedInt objectId, UnsignedInt pauseFrames, Int maxDist )
{
	if (!logEnabled())
		return;
	fprintf(stderr, "[AoC wander] frame %u %u pick failed pause=%u max=%d\n", TheGameLogic ? TheGameLogic->getFrame() : 0u,
		objectId, pauseFrames, (int)maxDist);
	fflush(stderr);
}

// ------------------------------------------------------------------------------------------------
void AoCWander::logUnstuck( UnsignedInt objectId, Int dist, Int maxDist )
{
	if (!logEnabled())
		return;
	fprintf(stderr, "[AoC wander] frame %u %u unstuck dist=%d max=%d\n", TheGameLogic ? TheGameLogic->getFrame() : 0u,
		objectId, (int)dist, (int)maxDist);
	fflush(stderr);
}
