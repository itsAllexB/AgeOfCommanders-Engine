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

// AoCSaveFidelity.h
// Age of Commanders F14a (design D11 sections 1.3 and 3.1): a game loaded from a save continues exactly as the saving
// game would have. Logic state the per-frame CRC covers, or that decides what happens next, that stock does not save:
//
//   1. The logic random generator (theGameLogicSeed and the base seed, RandomValue.cpp). Stock leaves a loaded game
//      drawing from whatever the process holds (the start-up time(nullptr) seed or the shell map's), and the seed is
//      part of the per-frame CRC.
//   2. The sleepy-update heap layout (GameLogic::m_sleepyUpdates). The scheduler compares only the wake frame and phase,
//      so modules due in the same frame run in heap-array order, which is the product of the game's history; stock
//      rebuilds the heap in object order on load (GameLogic::loadPostProcess), which runs ties in another order.
//   3. The AI groups (AIGroup and AI::xfer save nothing): their ids, which AIGroup::crc carries, the members, the speed
//      and dirty bit, and the group id counter.
//   4. The pathfinder's request queue, its head and tail and the cells examined this frame (Pathfinder::crc covers them,
//      Pathfinder::xfer writes only a version), the frame of the pending zone recalculation and the zones themselves:
//      the saving game recalculates them up to ZONE_UPDATE_FREQUENCY frames after the map changes, a load computes
//      them fresh, and a path search then examines other cells (every ground cell's zone, run-length packed, each
//      layer's zone and the zone manager's and blocks' tables; Pathfinder::getAoCZoneData).
//   5. Units' attack priority sets made by scripts during play: the sets load with the script engine, after the objects,
//      so AIUpdateInterface::xfer found none and kept the default set (any save version; deferAttackInfo).
//
// GameLogic::xfer version 15 writes, after the stock and earlier AoC fields (xfer below):
//   - the generator: six UnsignedInt words and the base seed (AoCLogicRandomState::xfer);
//   - the heap, keyed as stock Object::xfer keys module data, by the module's tag: the tag names the slots use (count as
//     UnsignedShort, then each name), the slot count (UnsignedInt), then for each slot in array order the object id
//     (ObjectID) and its module's index in that tag list (UnsignedShort), six bytes per sleepy module;
//   - the groups (xferGroups) and the pathfind queue (xferPathfindQueue).
// On load all of it is held here until the restore points:
//   - GameLogic::loadPostProcess calls restoreSleepyLayout instead of heapifying: every update module goes back to its
//     saved slot (found by object id and module tag, so a template whose module list changed order still matches) and
//     the heap property is checked. When the saved layout does not match the loaded modules (a module added to or
//     removed from a template since the save) the stock rebuild runs on the object order; when it matches but is not a
//     heap, the stock heapify runs on the saved order. Either fallback is deterministic;
//   - GameState::gameStatePostProcessLoad calls applyPendingLoad at its very end, after every other restore and every
//     load-time draw (object constructors, loadPostProcess functions): the attack sets, the pathfind queue, the groups,
//     then the generator, so the first logic frame of the loaded game draws exactly what the saving game drew next.
// A save of version 14 or older keeps the stock behaviour for 1 to 4. Nothing here is gated on the mod data: an
// uninterrupted game and a replay never run this code, and stock loads become reproducible too. Multiplayer: stock
// offers no save in a network game or a replay; if one is ever added, every peer restores the same state from the shared
// file, which is what keeps them in step after the load.
//
// The stock fixes of the same ticket, each marked "AoC @fix" or "AoC @hook ... (D11 F14)" in its file: Object::xfer sets the
// matrix only on load and (version 11) saves Thing's cached angle, direction and altitudes (Thing::xferAoCTransformCache);
// WeaponSet::xfer empties a slot saved empty; PartitionData::updateCellsTouched does not relook on a loaded object's
// first cell update, and PartitionManager::update leaves the undo queue alone during a load; AIUpdateInterface::xfer
// (version 6) saves the path timestamp and the blocked state, and its loadPostProcess registers the saved goal and
// position cells; Player::xfer (version 11) saves the retaliation mode; AIDockProcessDockState saves its next action
// frame and drone; and every save-only RETAIL_COMPATIBLE_XFER_SAVE site writes its newest version (AOC_FULL_XFER_SAVE,
// GameDefines.h: the dozer action machine, DozerAIUpdate, the turret machine, ExperienceTracker, delayed scripts, ...).
//
// While any harness flag is given, stderr gets "[AoC save] ..." lines at the save and at each restore (D11 F12);
// logic never reads them.

#pragma once

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include "Common/GameType.h"

#include <vector>

class Xfer;
class UpdateModule;

enum
{
	AOC_LOGIC_RANDOM_WORDS = 6,			///< words of the logic generator's state, besides the base seed
	AOC_SLEEPY_SLOT_NO_MODULE = 0,		///< module key of a heap entry without an object or a tag (NAMEKEY_INVALID)
};

/// The logic random generator's full state.
struct AoCLogicRandomState
{
	UnsignedInt seed[AOC_LOGIC_RANDOM_WORDS];
	UnsignedInt baseSeed;

	AoCLogicRandomState();

	static AoCLogicRandomState live();		///< the generator as it is now
	void apply() const;						///< make this the live generator
	UnsignedInt seedCRC() const;			///< the value GetGameLogicRandomSeedCRC returns once applied (the CRC's seed word)
	Bool operator==( const AoCLogicRandomState &other ) const;

	void xfer( Xfer *xfer );				///< the six words, then the base seed
};

/// One heap slot: which module of which object. The module is named by its tag, as stock Object::xfer names module data:
/// in memory by the tag's name key (Module::getModuleTagNameKey), in a save block's AoCSleepyLayout by the index of the
/// tag's name in the layout's tag list.
struct AoCSleepySlot
{
	ObjectID objectID;
	UnsignedInt moduleKey;

	AoCSleepySlot() : objectID( INVALID_ID ), moduleKey( AOC_SLEEPY_SLOT_NO_MODULE ) {}
	AoCSleepySlot( ObjectID id, UnsignedInt key ) : objectID( id ), moduleKey( key ) {}
	Bool operator==( const AoCSleepySlot &o ) const { return objectID == o.objectID && moduleKey == o.moduleKey; }
	Bool operator<( const AoCSleepySlot &o ) const
	{
		return objectID != o.objectID ? (Int)objectID < (Int)o.objectID : moduleKey < o.moduleKey;
	}
};

/// The heap layout as a save block holds it: the tag names, and the slots in heap-array order, each slot's moduleKey the
/// index of its tag in 'tags'.
struct AoCSleepyLayout
{
	std::vector<AsciiString> tags;
	std::vector<AoCSleepySlot> slots;
};

/// What restoring a saved heap layout comes to (AoCSaveFidelity::planLayout).
enum AoCLayoutPlan
{
	AOC_LAYOUT_NOT_MATCHED,		///< the saved slots and the loaded modules differ: the stock rebuild in object order
	AOC_LAYOUT_NOT_A_HEAP,		///< every module found, but in the saved order the array is no heap: heapify that order
	AOC_LAYOUT_RESTORED,		///< every module in its saved slot, a valid heap
};

/// One AI group as the save holds it (AIGroup has no save block of its own; AI::xfer writes nothing).
struct AoCSavedGroup
{
	UnsignedInt id;						///< the id the CRC carries (AIGroup::crc)
	Real speed;							///< as kept, not recomputed
	Bool dirty;
	std::vector<ObjectID> members;		///< in member-list order

	AoCSavedGroup() : id( 0 ), speed( 0.0f ), dirty( FALSE ) {}
};

/// The pathfinder's request queue as the CRC covers it (Pathfinder::crc): every slot, the head, the tail and the cells
/// examined this frame; and the frame of the pending zone recalculation. Stock Pathfinder::xfer writes only a version.
struct AoCPathfindQueue
{
	std::vector<ObjectID> requests;
	Int head;
	Int tail;
	Int cells;
	UnsignedInt zoneFrame;		///< PathfindZoneManager's next recalculation frame (0xFFFFFFFF: none pending)
	std::vector<unsigned char> zones;	///< Pathfinder::getAoCZoneData: the zones as last computed, packed

	AoCPathfindQueue() : head( 0 ), tail( 0 ), cells( 0 ), zoneFrame( 0xFFFFFFFF ) {}
};

namespace AoCSaveFidelity
{
	enum { XFER_FIRST_VERSION = 15 };		///< the GameLogic::xfer version that carries this data

	// --- pure parts (unit tests) ---------------------------------------------------------------------------------

	/// For each saved slot in order, the index in 'live' of the module with the same key. FALSE (order cleared) unless
	/// both lists hold the same keys, each exactly once.
	Bool matchSlots( const std::vector<AoCSleepySlot> &saved, const std::vector<AoCSleepySlot> &live, std::vector<Int> &order );

	/// TRUE when no entry has a lower priority value than its parent (parent of i is (i - 1) / 2): the binary min-heap the
	/// sleepy-update scheduler keeps, ties allowed.
	Bool isMinHeap( const std::vector<UnsignedInt> &priorities );

	/// The restore decision for a saved heap layout: 'saved' and 'live' keyed alike (object id, module tag key),
	/// 'livePriorities' the loaded modules' wake priorities in 'live' order. 'order' gets, for each saved slot, the index in
	/// 'live' of its module (empty when not matched). Keeps the scheduler's tie order exactly when it returns RESTORED.
	AoCLayoutPlan planLayout( const std::vector<AoCSleepySlot> &saved, const std::vector<AoCSleepySlot> &live,
		const std::vector<UnsignedInt> &livePriorities, std::vector<Int> &order );

	/// The heap layout of a save block: the tag count (UnsignedShort) and names, the slot count (UnsignedInt), then per
	/// slot the object id and the tag index (UnsignedShort). Load replaces 'layout'.
	void xferSlots( Xfer *xfer, AoCSleepyLayout &layout );

	/// The AI groups of a save block: the group id counter, the count, then per group its id, speed, dirty bit, member
	/// count and member ids. Load replaces both.
	void xferGroups( Xfer *xfer, UnsignedInt &lastGroupID, std::vector<AoCSavedGroup> &groups );

	/// The pathfind queue of a save block: the slot count, each slot, then head, tail, cells, the zone frame and the
	/// packed zones (byte count, bytes). Load replaces 'queue'.
	void xferPathfindQueue( Xfer *xfer, AoCPathfindQueue &queue );

	// --- engine glue -------------------------------------------------------------------------------------------------

	/// GameLogic::xfer, version 15 and later. Save: writes the live generator and the layout of 'heap'. Load: reads both
	/// and holds them for restoreSleepyLayout and applyPendingLoad.
	void xfer( Xfer *xfer, std::vector<UpdateModule *> &heap );

	/// GameLogic::xfer loading a save older than version 15: nothing to restore (drops anything held from an earlier load).
	void clearPending();

	/// GameState::loadGame, first: drops everything held, the deferred attack sets included, so nothing a failed earlier
	/// load left behind reaches this load's post-process.
	void beginLoad();

	/// GameLogic::xfer, at the start of a load's object data: drops the deferred attack sets of an earlier load.
	void beginObjectLoad();

	/// AIUpdateInterface::xfer on load: the unit's attack priority set 'name' does not exist yet (the script engine's sets,
	/// made by scripts during play, load after the objects); applyPendingLoad names it again. Any save version.
	void deferAttackInfo( ObjectID id, const AsciiString &name );

	/// GameLogic::loadPostProcess, with 'heap' holding every update module in object order (the stock collection pass).
	/// With a held layout that matches those modules (by object id and module tag), reorders 'heap' to the saved slots and
	/// sets each module's index; returns TRUE when the result is a valid heap. FALSE when there is no held layout or it
	/// does not match ('heap' untouched, in object order), or when the saved order is not a heap ('heap' left in the saved
	/// order): the caller then heapifies what 'heap' holds.
	Bool restoreSleepyLayout( std::vector<UpdateModule *> &heap );

	/// GameState::gameStatePostProcessLoad, last: the deferred attack sets, the AI groups, then the generator.
	void applyPendingLoad();
}
