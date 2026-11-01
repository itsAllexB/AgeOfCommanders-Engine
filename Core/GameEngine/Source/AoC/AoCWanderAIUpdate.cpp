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

// AoCWanderAIUpdate.cpp
// Age of Commanders F9: bounded wander for ambient civilians and oxen. See AoCWanderAIUpdate.h and AoCWander.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCWanderAIUpdate.h"
#include "Common/NameKeyGenerator.h"
#include "Common/RandomValue.h"
#include "Common/Xfer.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "WWMath/wwmath.h"

#include <stddef.h>

namespace
{
	// The logic random stream, the only source of the module's draws (D11 1.2).
	class LogicRandom : public AoCWanderRandom
	{
	public:
		virtual Int randomInt( Int lo, Int hi ) override { return GameLogicRandomValue(lo, hi); }
		virtual Real randomReal( Real lo, Real hi ) override { return GameLogicRandomValueReal(lo, hi); }
	};
}

//-------------------------------------------------------------------------------------------------
/*static*/ void AoCWanderAIUpdateModuleData::buildFieldParse( MultiIniFieldParse &p )
{
	AIUpdateModuleData::buildFieldParse(p);
	p.add(AoCWanderData::getFieldParse(), offsetof(AoCWanderAIUpdateModuleData, m_wander));
}

//-------------------------------------------------------------------------------------------------
AoCWanderAIUpdate::AoCWanderAIUpdate( Thing *thing, const ModuleData *moduleData ) : AIUpdateInterface( thing, moduleData )
{
}

//-------------------------------------------------------------------------------------------------
AoCWanderAIUpdate::~AoCWanderAIUpdate()
{
}

//-------------------------------------------------------------------------------------------------
/*static*/ Bool AoCWanderAIUpdate::isWanderer( const Object *obj )
{
	static const NameKeyType key = NAMEKEY("AoCWanderAIUpdate");
	const AIUpdateInterface *ai = obj ? obj->getAI() : nullptr;
	return ai != nullptr && ai->getModuleNameKey() == key;
}

//-------------------------------------------------------------------------------------------------
Int AoCWanderAIUpdate::maxDistForLog() const
{
	const AoCWanderData &data = getAoCWanderAIUpdateModuleData()->m_wander;
	return (Int)WWMath::Ceil(AoCWander::outerRadius(m_wander.clearance, data.wanderRadius));
}

//-------------------------------------------------------------------------------------------------
// The anchor is fixed once: the producer's position (SpawnBehavior sets the producer), with its footprint as the
// clearance unless ClearProducerFootprint = No, when AnchorToProducer is on and the producer exists; else the walker's
// own position with clearance 0.
void AoCWanderAIUpdate::fixAnchor()
{
	const AoCWanderData &data = getAoCWanderAIUpdateModuleData()->m_wander;
	Object *obj = getObject();
	Coord3D anchor = *obj->getPosition();
	Real clearance = 0.0f;
	if (data.anchorToProducer)
	{
		const Object *producer = TheGameLogic->findObjectByID(obj->getProducerID());
		if (producer != nullptr && producer != obj)
		{
			anchor = *producer->getPosition();
			clearance = AoCWander::anchorClearance(TRUE, data.clearProducerFootprint, producer->getGeometryInfo().getBoundingCircleRadius());
		}
	}
	AoCWander::fixAnchor(m_wander, TheGameLogic->getFrame(), anchor, clearance, *obj->getPosition());
}

//-------------------------------------------------------------------------------------------------
// Pick a point and issue the move. The pathfinder moves a destination off obstacles and other units' goals; the
// adjusted spot is taken only within one pathfinder cell of the outer radius, so a pick that falls inside another
// building cannot send the walker round to its far side. When no draw gives such a spot, pause again (and say so).
void AoCWanderAIUpdate::pickAndWalk( UnsignedInt now )
{
	const AoCWanderData &data = getAoCWanderAIUpdateModuleData()->m_wander;
	Object *obj = getObject();
	const Real outer = AoCWander::outerRadius(m_wander.clearance, data.wanderRadius);
	LogicRandom random;
	for (Int attempt = 0; attempt < AOC_WANDER_PICK_ATTEMPTS; ++attempt)
	{
		Coord3D dest = AoCWander::drawPoint(m_wander.anchor, m_wander.clearance, data.wanderRadius, random);
		if (!TheAI->pathfinder()->adjustDestination(obj, getLocomotorSet(), &dest))
			continue;
		if (!AoCWander::withinReach(m_wander.anchor, outer, PATHFIND_CELL_SIZE_F, dest))
			continue;
		aiMoveToPosition(&dest, CMD_FROM_AI);
		AoCWander::moveStarted(m_wander, now, *obj->getPosition(), dest);
		return;
	}
	const UnsignedInt pause = AoCWander::drawPause(data.minPauseFrames, data.maxPauseFrames, random);
	AoCWander::pauseFor(m_wander, now, pause);
	if (AoCWander::logEnabled())
		AoCWander::logPickFailed(obj->getID(), pause, maxDistForLog());
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime AoCWanderAIUpdate::update()
{
	const AoCWanderData &data = getAoCWanderAIUpdateModuleData()->m_wander;
	Object *obj = getObject();
	const UnsignedInt now = TheGameLogic->getFrame();
	const Bool dead = obj->isEffectivelyDead();

	if (!m_wander.anchored)
		fixAnchor();

	// 1. The watchdog: a walker sitting in a stock move without getting anywhere for MaxPause frames (two units locked
	// on one spot by the stock collision code) is sent idle and picks again. Panic, pushes and death are other states.
	if (!dead && AoCWander::watchStuck(m_wander, now, getAIStateType() == AI_MOVE_TO, *obj->getPosition(), data.maxPauseFrames))
	{
		if (AoCWander::logEnabled())
			AoCWander::logUnstuck(obj->getID(), AoCWander::distanceForLog(m_wander.anchor, *obj->getPosition()), maxDistForLog());
		aiIdle(CMD_FROM_AI);
	}

	// 2. The pause is over and the stock AI has nothing else to do: walk.
	if (AoCWander::wantsMove(m_wander, now, isIdle(), dead))
		pickAndWalk(now);

	// 3. extend
	UpdateSleepTime sleep = AIUpdateInterface::update();

	// 4. The move has ended: arrived, failed, or replaced by stock behaviour (panic, a push, a repulsor) that has since
	// finished. Pause, then pick again.
	if (AoCWander::moveEnded(m_wander, isIdle()))
	{
		LogicRandom random;
		const UnsignedInt pause = AoCWander::drawPause(data.minPauseFrames, data.maxPauseFrames, random);
		AoCWander::pauseFor(m_wander, now, pause);
		if (AoCWander::logEnabled())
			AoCWander::logArrival(obj->getID(), AoCWander::distanceForLog(m_wander.anchor, *obj->getPosition()),
				AoCWander::distanceForLog(m_wander.anchor, m_wander.dest), pause, maxDistForLog());
	}

	// 5. Sleep the rest of the pause; while a move is in flight (or the AI is busy past the pause) look again after a
	// quarter of MinPause. The stock AI's own request wins when it is sooner (every frame while the locomotor runs).
	const UnsignedInt mine = AoCWander::sleepFrames(m_wander, now, data.minPauseFrames);
	if (UPDATE_SLEEP(mine) < sleep)
		sleep = UPDATE_SLEEP(mine);
	return sleep;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AoCWanderAIUpdate::crc( Xfer *xfer )
{
	// extend base class
	AIUpdateInterface::crc(xfer);
	m_wander.crc(xfer);
}

//-------------------------------------------------------------------------------------------------
/*static*/ void AoCWanderAIUpdate::xferStockAI( Xfer *xfer, void *context )
{
	static_cast<AoCWanderAIUpdate *>(context)->AIUpdateInterface::xfer(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Xfer method: AoCWanderState::xferBlock (version 1: the stock AI block, then the walker's fields) */
// ------------------------------------------------------------------------------------------------
void AoCWanderAIUpdate::xfer( Xfer *xfer )
{
	m_wander.xferBlock(xfer, xferStockAI, this);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AoCWanderAIUpdate::loadPostProcess()
{
	// extend base class
	AIUpdateInterface::loadPostProcess();
}
