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
// The anchor is fixed once: the producer's position (SpawnBehavior sets the producer) with its footprint as the
// clearance, when AnchorToProducer is on and the producer exists, else the walker's own position.
void AoCWanderAIUpdate::fixAnchor()
{
	const AoCWanderData &data = getAoCWanderAIUpdateModuleData()->m_wander;
	Object *obj = getObject();
	m_wander.anchor = *obj->getPosition();
	m_wander.clearance = 0.0f;
	if (data.anchorToProducer)
	{
		const Object *producer = TheGameLogic->findObjectByID(obj->getProducerID());
		if (producer != nullptr && producer != obj)
		{
			m_wander.anchor = *producer->getPosition();
			m_wander.clearance = producer->getGeometryInfo().getBoundingCircleRadius();
		}
	}
	m_wander.anchored = TRUE;
	m_wander.nextMoveFrame = TheGameLogic->getFrame();
}

//-------------------------------------------------------------------------------------------------
// Pick a point and issue the move. The pathfinder moves a destination off obstacles and other units' goals; the
// adjusted spot is taken only within one pathfinder cell of the outer radius, so a pick that falls inside another
// building cannot send the walker round to its far side. FALSE when no draw gave such a spot.
Bool AoCWanderAIUpdate::startMove()
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
		return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime AoCWanderAIUpdate::update()
{
	const AoCWanderData &data = getAoCWanderAIUpdateModuleData()->m_wander;
	Object *obj = getObject();
	const UnsignedInt now = TheGameLogic->getFrame();
	LogicRandom random;

	if (!m_wander.anchored)
		fixAnchor();

	// The pause is over and the stock AI has nothing else to do: walk.
	if (!m_wander.moving && now >= m_wander.nextMoveFrame && isIdle() && !obj->isEffectivelyDead())
	{
		if (startMove())
			m_wander.moving = TRUE;
		else
			m_wander.nextMoveFrame = now + AoCWander::drawPause(data.minPauseFrames, data.maxPauseFrames, random);
	}

	// extend
	UpdateSleepTime sleep = AIUpdateInterface::update();

	// The move has ended: arrived, failed, or replaced by stock behaviour (panic, a push, a repulsor) that has since
	// finished. Pause, then pick again.
	if (m_wander.moving && isIdle())
	{
		m_wander.moving = FALSE;
		const UnsignedInt pause = AoCWander::drawPause(data.minPauseFrames, data.maxPauseFrames, random);
		m_wander.nextMoveFrame = now + pause;
		if (AoCWander::logEnabled())
		{
			const Int maxDist = (Int)WWMath::Ceil(AoCWander::outerRadius(m_wander.clearance, data.wanderRadius));
			AoCWander::logArrival(obj->getID(), AoCWander::distanceForLog(m_wander.anchor, *obj->getPosition()), pause, maxDist);
		}
	}

	// Sleep the rest of the pause; while a move is in flight (or the AI is busy past the pause) look again after a
	// quarter of MinPause. The stock AI's own request wins when it is sooner (every frame while the locomotor runs).
	UnsignedInt mine;
	if (!m_wander.moving && now < m_wander.nextMoveFrame)
		mine = m_wander.nextMoveFrame - now;
	else
		mine = AoCWander::checkFrames(data.minPauseFrames);
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

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version: the stock AI, then the anchor, its clearance, the next move frame and the move flag */
// ------------------------------------------------------------------------------------------------
void AoCWanderAIUpdate::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	AIUpdateInterface::xfer(xfer);
	m_wander.xfer(xfer);
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AoCWanderAIUpdate::loadPostProcess()
{
	// extend base class
	AIUpdateInterface::loadPostProcess();
}
