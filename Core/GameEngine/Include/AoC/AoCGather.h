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

// AoCGather.h
// Age of Commanders F2 (design D11 section 2.2): typed gathering. The stock supply loop (warehouse, truck or
// worker, centre) carries one resource type at a time. "Boxes" stay the unit of cargo: a tree is a warehouse
// with StartingBoxes = 200 of wood, and a worker picks one unit per gather delay up to its per-type capacity.
// This file holds the pure parts the three stock modules share, so the unit tests can run them:
//
//   Behavior = SupplyWarehouseDockUpdate ModuleTag_x   ; a node
//     AoCResourceType  = Wood        ; the node's type (default Cash)
//     AoCKillWhenEmpty = Yes         ; kill() at zero so Die modules run (default No)
//   End
//   Behavior = SupplyCenterDockUpdate ModuleTag_y      ; a drop-off
//     AoCAccepts      = Food Wood Iron   ; types it takes (default Cash only)
//     AoCValuePerUnit = 1                ; bank units per carried unit, every type but cash (default 1, at least 1)
//   End
//   Behavior = WorkerAIUpdate ModuleTag_z               ; also SupplyTruckAIUpdate, ChinookAIUpdate
//     AoCCargoTypes  = Food Wood Iron Cash   ; types it may carry (default Cash only)
//     AoCMaxUnits    = Food:10 Wood:10       ; capacity per type, each at least 1 (else MaxBoxes)
//     AoCGatherDelay = Wood:3000 Iron:5000   ; milliseconds per unit (else SupplyWarehouseActionDelay)
//   End
//
// Type names resolve through TheAoCRules at parse time, like AoCCost. With no AoCResourceType defined every
// field is cash and every hook takes the stock path with the stock arithmetic.

#pragma once

#include "AoC/AoCCost.h"
#include "Lib/BaseType.h"

class INI;
class Player;
class Xfer;

/// Module data of a gatherer (SupplyTruckAIUpdate, WorkerAIUpdate; the Chinook inherits the supply truck's).
struct AoCCargoData
{
	UnsignedInt cargoMask;										///< AoCCargoTypes: one bit per type it may carry; cash only by default
	UnsignedInt maxUnitsMask;									///< types with an AoCMaxUnits entry
	UnsignedInt maxUnits[AOC_MAX_RESOURCES];
	UnsignedInt gatherDelayMask;							///< types with an AoCGatherDelay entry
	UnsignedInt gatherDelayFrames[AOC_MAX_RESOURCES];	///< milliseconds converted at parse time, as INI::parseDurationUnsignedInt does

	AoCCargoData();

	Bool canCarry( Int type ) const;
	/// AoCMaxUnits for the type, else the stock MaxBoxes.
	Int maxUnitsFor( Int type, Int stockMaxBoxes ) const;
	/// AoCGatherDelay for the type, else the stock SupplyWarehouseActionDelay (frames either way).
	UnsignedInt gatherDelayFor( Int type, UnsignedInt stockDelayFrames ) const;

	/// Set from parsed "Name:amount" lists (the INI parsers and the unit tests).
	void setCargoTypes( UnsignedInt mask ) { cargoMask = mask; }
	/// Every entry must be at least 1 (INI_INVALID_DATA otherwise): a capacity of 0 for a carried type would let the
	/// gatherer dock and fail the pick-up forever. A stock MaxBoxes of 0 stays stock.
	void setMaxUnits( const AoCCost &units, UnsignedInt presentMask );
	void setGatherDelayMsecs( const AoCCost &msecs, UnsignedInt presentMask );

	// FieldParse procedures; 'store' points at the AoCCargoData.
	static void parseCargoTypes( INI *ini, void *instance, void *store, const void *userData );
	static void parseMaxUnits( INI *ini, void *instance, void *store, const void *userData );
	static void parseGatherDelay( INI *ini, void *instance, void *store, const void *userData );
};

/// Runtime fields on the gatherer's AI module (m_aocCargoType, m_aocAssignedType of D11), saved by the module's xfer
/// at its new version and written into its crc.
struct AoCCargoState
{
	Int cargoType;		///< type of the units in m_numberBoxes; cash until the first typed pick-up
	Int assignedType;	///< type of the last node the player sent it to; -1 none

	AoCCargoState();

	/// Two Ints. On load the saved indices map through TheAoCRules (saved ahead of the objects by PlayerList::xfer),
	/// so a save made with another type set loads by name; a type that is gone becomes cash (cargo) or none (assignment).
	void xfer( Xfer *xfer );
	/// The module's versioned block: the two fields only when the block's version is sinceVersion or newer, so an
	/// older save (a truck block before 2, a worker block before 3) keeps the constructor values, cash and unassigned.
	void xferSince( Xfer *xfer, UnsignedByte blockVersion, UnsignedByte sinceVersion );
	/// Both fields while resource types are defined; nothing with stock data (the F1 bank's CRC rule).
	void crc( Xfer *xfer );
};

namespace AoCGather
{
	inline UnsignedInt typeBit( Int type ) { return (type >= 0 && type < AOC_MAX_RESOURCES) ? (1u << type) : 0u; }

	/// D2 3.1, switching type discards the load: TRUE when a pick-up from a node of nodeType must first drop what
	/// the gatherer carries.
	Bool mustDropBeforePickup( Int cargoType, Int cargoCount, Int nodeType );

	/// Node search (ResourceGatheringManager): a gatherer the player assigned wants that type only; an unassigned
	/// one any type it may carry.
	Bool wantsNodeType( Int nodeType, Int assignedType, UnsignedInt cargoMask );

	/// A rate of perMille scales the per-unit delay by 1000 / perMille (1100 is 10 percent faster), integer
	/// arithmetic. 1000 is the identity.
	UnsignedInt scaleDelayByRate( UnsignedInt delayFrames, Int ratePerMille );

	/// The per-unit dock delay of a gatherer at a node, for every gatherer module (the truck, the worker; the Chinook
	/// through the truck's): AoCGatherDelay for the node's type, else the stock SupplyWarehouseActionDelay, then the
	/// owner's gather rate. This is F13's one multiplication point (per-type gather upgrades, horde bonus): the rate
	/// is 1000 per mille until F13 adds Player::m_aocGatherPerMille. 'owner' is unused until then.
	UnsignedInt gatherDelayFrames( const AoCCargoData &cargo, Int nodeType, UnsignedInt stockDelayFrames, const Player *owner );

	/// Milliseconds to frames with the INI duration rule (ceiling of ms * LOGICFRAMES_PER_MSEC_REAL), at the live
	/// simulation rate, which is fixed before any INI loads (F15).
	UnsignedInt msecsToFrames( UnsignedInt msecs );

	/// One type name to its index; INI_INVALID_DATA for a name that is not a defined AoCResourceType.
	Int parseTypeName( const char *name );
	/// Whitespace-separated type names to a mask (unit tests and tools); INI_INVALID_DATA on an unknown name.
	UnsignedInt parseTypeListText( const char *text );

	// FieldParse procedures.
	/// One type name into an Int (AoCResourceType).
	void parseResourceType( INI *ini, void *instance, void *store, const void *userData );
	/// A list of type names into an UnsignedInt mask (AoCAccepts, AoCCargoTypes).
	void parseResourceTypeList( INI *ini, void *instance, void *store, const void *userData );
	/// A whole number of at least 1 into an UnsignedInt (AoCValuePerUnit): a negative value or 0 is INI_INVALID_DATA,
	/// which the INI loader reports with the file, line and field, like an unknown type name.
	void parsePositiveUnsignedInt( INI *ini, void *instance, void *store, const void *userData );

	/// Type name for logs, "?" when out of range.
	const char *typeName( Int type );

	/// Evidence trail (ticket F2 decision 4): while any harness flag is given, one stderr line per gather event,
	/// "[AoC gather] frame N <objectId> <event>". Nothing is printed in a stock run; logic never reads it.
	Bool logEnabled();
	void log( UnsignedInt objectId, const char *format, ... );
}
