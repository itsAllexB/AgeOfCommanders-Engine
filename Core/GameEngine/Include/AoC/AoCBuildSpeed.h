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

// AoCBuildSpeed.h
// Age of Commanders F8 (design D11 section 2.8, D5 sections 2 and 5): per-builder construction speed and back to work.
//
//   Behavior = DozerAIUpdate ModuleTag_x        ; also WorkerAIUpdate
//     AoCBuildSpeedPerMille = 1500              ; this builder's construction speed, per mille of the stock rate
//   End                                         ; (Int, default 1000 = the stock rate; a value below 1 is an INI error)
//
// D5's values: builder 1000, GLA worker 600, dozer and Foreman 1500.
//
// Stock construction (DozerActionDoActionState::update, task DOZER_TASK_BUILD; workers run the same dozer state
// machine) adds 100 / framesToBuild to the structure's construction percent and maxHealth / framesToBuild to its
// health on every frame a builder works at it, framesToBuild being calcTimeToBuild of the structure (build time at the
// live simulation rate, handicap, faction modifier, low power). Both increments go through scale() with the working
// builder's per mille, which DozerAIInterface::getAoCBuildSpeedPerMille() reports. Each working builder runs the line,
// so builders on one structure would add up; note that the stock ActionManager::canResumeConstructionOf refuses a
// second builder while one is at work, so in the stock rules one builder works at a time and a scaffold changes hands
// only when its builder stops (dies, is ordered away).
//
// Back to work: WorkerAIUpdate::internalTaskComplete(DOZER_TASK_BUILD) forces the worker's supply brain into wanting
// (setForceWantingState, as a new truck from a supply centre gets) when the worker has an F2 assigned resource type,
// no other dozer task is pending and a node of that type stands within GameData.ini AoCBackToWorkRange (Int, world
// units, default 150, D5 section 5) of the build site (backToWork below); the supply brain then docks at the nearest
// node of that type. Otherwise the worker idles as in stock, and so does the dozer, which has no supply brain. Under
// the AoC rules a later order wins over the latch: WorkerAIUpdate::newTask and every player command but a dock clear it
// (review F8, finding 1).
//
// No new saved or CRC state: the speed is template data. With no template carrying the key every builder reports 1000,
// scale() returns the stock increment untouched and the stock game is bit-identical.

#pragma once

#include "Lib/BaseType.h"

class INI;

enum
{
	AOC_BUILD_SPEED_DEFAULT_PER_MILLE = 1000,	///< the stock construction rate
	AOC_BACK_TO_WORK_DEFAULT_RANGE = 150,			///< GameData.ini AoCBackToWorkRange default, world units (D5 section 5)
	AOC_BUILD_LOG_OBJECTS = 128,							///< structures the harness build log follows at once
	AOC_BUILD_LOG_BUILDERS = 8,								///< builders remembered per structure for the log
};

/// What a worker does when its build task ends (AoCBuildSpeed::backToWork).
enum AoCBackToWork
{
	AOC_BACK_TO_WORK_GO,							///< force the supply brain into wanting: it docks at the nearest node of the type
	AOC_BACK_TO_WORK_UNASSIGNED,			///< no assigned type (every AI worker in B1, every worker with stock data): idle
	AOC_BACK_TO_WORK_TASK_PENDING,		///< another dozer task (a queued repair) runs first
	AOC_BACK_TO_WORK_NONE_IN_RANGE,		///< no node of the type within AoCBackToWorkRange of the build site: idle
};

namespace AoCBuildSpeed
{
	/// The one multiplication point (D11 1.2): the stock per-frame increment times perMille / 1000, computed as
	/// increment * perMille / 1000.0f. perMille == 1000 returns the increment itself, without any arithmetic, so the
	/// stock game stays bit-identical.
	Real scale( Real increment, Int perMille );

	/// FieldParse procedure of AoCBuildSpeedPerMille: a whole number of at least 1, else INI_INVALID_DATA (the INI loader
	/// names the file and line).
	void parsePerMille( INI *ini, void *instance, void *store, const void *userData );

	/// FieldParse procedure of GameData.ini AoCBackToWorkRange: a whole number of at least 0, else INI_INVALID_DATA.
	void parseRange( INI *ini, void *instance, void *store, const void *userData );

	/// Back to work (D5 section 5), decided when a worker's build task ends through internalTaskComplete: the
	/// structure is complete, or the worker found another builder at work on it (the stock "can't double up" end). A
	/// cancelled task (the scaffold sold or destroyed, the worker ordered away) ends through internalCancelTask, which
	/// never asks. assignedType is the F2 assigned type (-1 none); otherTaskPending: a dozer task is still queued (a
	/// repair); nodeOfTypeInRange: a node of the assigned type within AoCBackToWorkRange of the build site.
	AoCBackToWork backToWork( Int assignedType, Bool otherTaskPending, Bool nodeOfTypeInRange );

	/// TRUE when b is within range of a in 2D, squares compared (no square root in logic, D11 1.2).
	Bool withinRange( const Coord3D &a, const Coord3D &b, Real range );

	// --- harness evidence (any harness flag given and resource types defined); nothing with stock data; logic never reads it ---
	//   "[AoC build] frame N <builderId> start <template> <objectId> speed=<perMille>"
	//        a builder starts constructing: its first construction frame on that structure, or its first after a gap
	//        (it was away and came back). On a structure with progress and no start line in this process (a saved game
	//        loaded mid-construction) the line ends " resumed progress=<percent before this frame>".
	//   "[AoC build] frame N <objectId> complete <template> builders=<n> frames=<k> base=<framesToBuild>"
	//        construction complete: n distinct builders worked at it since its first start line; k frames from the first
	//        start line to this one, both counted (the number of construction frames of one uninterrupted builder);
	//        base is calcTimeToBuild on the last frame, the frames one builder at 1000 needs.
	//   "[AoC build] frame N <builderId> back-to-work <type>"
	//        a worker's build task ended and its supply brain was sent back to its assigned type.
	//   "[AoC build] frame N <builderId> back-to-work none in range <type> range=<AoCBackToWorkRange>"
	//        a worker with an assigned type ended a build with no node of that type within the range: it idles.
	Bool logEnabled();
	void logBuildFrame( UnsignedInt builderId, UnsignedInt objectId, const char *templateName, Int perMille, Real percentBefore );
	void logComplete( UnsignedInt objectId, const char *templateName, Int framesToBuild );
	void logBackToWork( UnsignedInt builderId, const char *typeName );
	void logBackToWorkNoneInRange( UnsignedInt builderId, const char *typeName, Int range );
}

/// The build log's bookkeeping, apart from printing so the unit tests can drive it: which structures are under
/// construction, since which frame, by which builders. Fixed storage (no allocation): AOC_BUILD_LOG_OBJECTS structures,
/// the least recently worked one making room when full; AOC_BUILD_LOG_BUILDERS builders each. Keyed by ObjectID; a frame
/// earlier than the last one seen (a new game in the same process) forgets everything.
class AoCBuildLog
{
public:
	enum StartKind
	{
		NOT_A_START,		///< the builder also worked at this structure on the previous frame
		START,					///< the builder starts (or starts again after a gap)
		RESUMED,				///< the first construction frame seen for a structure that already has progress
	};

	AoCBuildLog();
	void reset();

	/// A builder works at a structure on this frame. percentBefore is the structure's construction percent before the
	/// builder's increment.
	StartKind onBuildFrame( UnsignedInt frame, UnsignedInt builderId, UnsignedInt objectId, Real percentBefore );

	/// The structure is complete on this frame: the distinct builders since its first start line and the frames from
	/// that line to this one, both counted; the structure is forgotten. FALSE when it was never seen.
	Bool onComplete( UnsignedInt frame, UnsignedInt objectId, Int *builders, UnsignedInt *frames );

private:
	struct Entry
	{
		UnsignedInt objectId;		///< 0: free
		UnsignedInt firstFrame;
		UnsignedInt lastFrame;	///< the last frame any builder worked at it
		Int builderCount;				///< distinct builders, also beyond the ones remembered
		UnsignedInt builderIds[AOC_BUILD_LOG_BUILDERS];
		UnsignedInt builderLastFrame[AOC_BUILD_LOG_BUILDERS];
	};
	Entry *find( UnsignedInt objectId );
	Entry *make( UnsignedInt objectId, UnsignedInt frame );

	Entry m_entries[AOC_BUILD_LOG_OBJECTS];
	UnsignedInt m_lastFrame;
};
