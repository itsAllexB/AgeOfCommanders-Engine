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

// AoCBuildSpeed.cpp
// Age of Commanders F8 (design D11 section 2.8): per-builder construction speed and back to work. See AoCBuildSpeed.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCBuildSpeed.h"
#include "AoC/AoCHarness.h"
#include "AoC/AoCRules.h"
#include "Common/INI.h"
#include "GameLogic/GameLogic.h"

#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------
Real AoCBuildSpeed::scale( Real increment, Int perMille )
{
	// The stock rate is the stock increment itself: no multiplication, so 1000 is bit-identical on every compiler.
	if (perMille == AOC_BUILD_SPEED_DEFAULT_PER_MILLE)
		return increment;
	// One Real times an integer ratio, in single precision, left to right; no transcendental (D11 1.2).
	return increment * (Real)perMille / 1000.0f;
}

// ------------------------------------------------------------------------------------------------
void AoCBuildSpeed::parsePerMille( INI * /*ini*/, void * /*instance*/, void *store, const void * /*userData*/ )
{
	const char *token = INI::getNextToken();
	const Int value = INI::scanInt(token);
	if (value < 1)
	{
		DEBUG_CRASH(("AoCBuildSpeedPerMille: '%s' must be a whole number of at least 1", token));
		throw INI_INVALID_DATA;
	}
	*(Int *)store = value;
}

// ------------------------------------------------------------------------------------------------
void AoCBuildSpeed::parseRange( INI * /*ini*/, void * /*instance*/, void *store, const void * /*userData*/ )
{
	const char *token = INI::getNextToken();
	const Int value = INI::scanInt(token);
	if (value < 0)
	{
		DEBUG_CRASH(("AoCBackToWorkRange: '%s' must be a whole number of at least 0", token));
		throw INI_INVALID_DATA;
	}
	*(Int *)store = value;
}

// ------------------------------------------------------------------------------------------------
AoCBackToWork AoCBuildSpeed::backToWork( Int assignedType, Bool otherTaskPending, Bool nodeOfTypeInRange )
{
	if (assignedType < 0)
		return AOC_BACK_TO_WORK_UNASSIGNED;
	if (otherTaskPending)
		return AOC_BACK_TO_WORK_TASK_PENDING;
	if (!nodeOfTypeInRange)
		return AOC_BACK_TO_WORK_NONE_IN_RANGE;
	return AOC_BACK_TO_WORK_GO;
}

// ------------------------------------------------------------------------------------------------
Bool AoCBuildSpeed::withinRange( const Coord3D &a, const Coord3D &b, Real range )
{
	const Real dx = b.x - a.x;
	const Real dy = b.y - a.y;
	return dx * dx + dy * dy <= range * range;
}

// ------------------------------------------------------------------------------------------------
// The build log's bookkeeping
// ------------------------------------------------------------------------------------------------
AoCBuildLog::AoCBuildLog()
{
	reset();
}

// ------------------------------------------------------------------------------------------------
void AoCBuildLog::reset()
{
	memset(m_entries, 0, sizeof(m_entries));
	m_lastFrame = 0;
}

// ------------------------------------------------------------------------------------------------
AoCBuildLog::Entry *AoCBuildLog::find( UnsignedInt objectId )
{
	for (Int i = 0; i < AOC_BUILD_LOG_OBJECTS; ++i)
		if (m_entries[i].objectId == objectId)
			return &m_entries[i];
	return nullptr;
}

// ------------------------------------------------------------------------------------------------
AoCBuildLog::Entry *AoCBuildLog::make( UnsignedInt objectId, UnsignedInt frame )
{
	// a free entry, else the one worked at least recently (a scaffold abandoned or destroyed before completion)
	Entry *pick = &m_entries[0];
	for (Int i = 0; i < AOC_BUILD_LOG_OBJECTS; ++i)
	{
		if (m_entries[i].objectId == 0)
		{
			pick = &m_entries[i];
			break;
		}
		if (m_entries[i].lastFrame < pick->lastFrame)
			pick = &m_entries[i];
	}
	memset(pick, 0, sizeof(*pick));
	pick->objectId = objectId;
	pick->firstFrame = frame;
	pick->lastFrame = frame;
	return pick;
}

// ------------------------------------------------------------------------------------------------
AoCBuildLog::StartKind AoCBuildLog::onBuildFrame( UnsignedInt frame, UnsignedInt builderId, UnsignedInt objectId, Real percentBefore )
{
	if (frame < m_lastFrame)
		reset();	// a new game in the same process: the frames start again
	m_lastFrame = frame;

	Entry *e = find(objectId);
	const Bool isNew = (e == nullptr);
	if (isNew)
		e = make(objectId, frame);
	e->lastFrame = frame;

	for (Int i = 0; i < e->builderCount && i < AOC_BUILD_LOG_BUILDERS; ++i)
	{
		if (e->builderIds[i] != builderId)
			continue;
		const Bool continuing = (e->builderLastFrame[i] + 1 == frame) || (e->builderLastFrame[i] == frame);
		e->builderLastFrame[i] = frame;
		return continuing ? NOT_A_START : START;
	}
	if (e->builderCount < AOC_BUILD_LOG_BUILDERS)
	{
		e->builderIds[e->builderCount] = builderId;
		e->builderLastFrame[e->builderCount] = frame;
	}
	++e->builderCount;
	return (isNew && percentBefore > 0.0f) ? RESUMED : START;
}

// ------------------------------------------------------------------------------------------------
Bool AoCBuildLog::onComplete( UnsignedInt frame, UnsignedInt objectId, Int *builders, UnsignedInt *frames )
{
	Entry *e = find(objectId);
	if (e == nullptr)
	{
		*builders = 0;
		*frames = 0;
		return FALSE;
	}
	*builders = e->builderCount;
	*frames = frame - e->firstFrame + 1;
	memset(e, 0, sizeof(*e));
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// Harness lines
// ------------------------------------------------------------------------------------------------
namespace
{
	AoCBuildLog &buildLog()
	{
		static AoCBuildLog log;	// fixed storage, nothing allocated
		return log;
	}

	UnsignedInt logFrame()
	{
		return TheGameLogic ? TheGameLogic->getFrame() : 0u;
	}
}

// ------------------------------------------------------------------------------------------------
Bool AoCBuildSpeed::logEnabled()
{
	// with stock data (no resource type defined) nothing is printed, flags or not (D11 F8 decision 4)
	return AoCHarness::isActive() && AoCRules::enabled();
}

// ------------------------------------------------------------------------------------------------
void AoCBuildSpeed::logBuildFrame( UnsignedInt builderId, UnsignedInt objectId, const char *templateName, Int perMille, Real percentBefore )
{
	const UnsignedInt frame = logFrame();
	const AoCBuildLog::StartKind kind = buildLog().onBuildFrame(frame, builderId, objectId, percentBefore);
	if (kind == AoCBuildLog::NOT_A_START)
		return;
	if (kind == AoCBuildLog::RESUMED)
		fprintf(stderr, "[AoC build] frame %u %u start %s %u speed=%d resumed progress=%.2f\n", frame, builderId, templateName, objectId,
			perMille, (double)percentBefore);
	else
		fprintf(stderr, "[AoC build] frame %u %u start %s %u speed=%d\n", frame, builderId, templateName, objectId, perMille);
	fflush(stderr);
}

// ------------------------------------------------------------------------------------------------
void AoCBuildSpeed::logComplete( UnsignedInt objectId, const char *templateName, Int framesToBuild )
{
	const UnsignedInt frame = logFrame();
	Int builders = 0;
	UnsignedInt frames = 0;
	buildLog().onComplete(frame, objectId, &builders, &frames);
	fprintf(stderr, "[AoC build] frame %u %u complete %s builders=%d frames=%u base=%d\n", frame, objectId, templateName, builders, frames,
		framesToBuild);
	fflush(stderr);
}

// ------------------------------------------------------------------------------------------------
void AoCBuildSpeed::logBackToWork( UnsignedInt builderId, const char *typeName )
{
	fprintf(stderr, "[AoC build] frame %u %u back-to-work %s\n", logFrame(), builderId, typeName);
	fflush(stderr);
}

// ------------------------------------------------------------------------------------------------
void AoCBuildSpeed::logBackToWorkNoneInRange( UnsignedInt builderId, const char *typeName, Int range )
{
	fprintf(stderr, "[AoC build] frame %u %u back-to-work none in range %s range=%d\n", logFrame(), builderId, typeName, range);
	fflush(stderr);
}
