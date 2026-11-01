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
// (setForceWantingState, as a new truck from a supply centre gets) when the worker has an F2 assigned resource type
// and no other dozer task is pending; the supply brain then docks at the nearest node of that type within its scan
// distance. A worker without an assigned type, and the dozer, which has no supply brain, idle as in stock.
//
// No new saved or CRC state: the speed is template data. With no template carrying the key every builder reports 1000,
// scale() returns the stock increment untouched and the stock game is bit-identical.

#pragma once

#include "Lib/BaseType.h"

class INI;

enum
{
	AOC_BUILD_SPEED_DEFAULT_PER_MILLE = 1000,	///< the stock construction rate
	AOC_BUILD_LOG_OBJECTS = 128,							///< structures the harness build log follows at once
	AOC_BUILD_LOG_BUILDERS = 8,								///< builders remembered per structure for the log
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
	Bool logEnabled();
	void logBuildFrame( UnsignedInt builderId, UnsignedInt objectId, const char *templateName, Int perMille, Real percentBefore );
	void logComplete( UnsignedInt objectId, const char *templateName, Int framesToBuild );
	void logBackToWork( UnsignedInt builderId, const char *typeName );
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
