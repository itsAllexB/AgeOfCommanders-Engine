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

// AoCPopulation.h
// Age of Commanders F4 (design D11 section 2.4, D3 sections 1 and 7): the per-player population. It lives
// in Player next to the resource bank and is modelled on Energy: objects add and remove themselves as they
// join and leave the player (Player::becomingTeamMember), so capture, defect, hijack and death all work.
//
//   used      the AoCPopulationCost of every object the player owns, contained units included
//   capacity  the AoCPopulationCapacity of every owned object that is not under construction
//   reserved  the population of units queued in the player's factories and not yet out of the door, each with the
//             units it is created with (a transport's initial payload)
//   cap       capacity, clamped to the lobby cap (GameLogic::getAoCPopulationCap; 0 means no lobby clamp)
//   headroom  cap - used - reserved; negative when the player is over the cap (destroyed houses, captures)
//
// Production queues a unit only while the headroom covers its population (BuildAssistant::canMakeUnit,
// CANMAKE_NO_HOUSING). Going over the cap is allowed and nothing dies; production waits.
//
// The data is the flag (D11 1.6): with every template at AoCPopulationCost 0 and AoCPopulationCapacity 0
// the counters stay 0, the player is never "at cap", nothing enters the CRC and no production is refused,
// so the stock game is unchanged. All counts are integers.

#pragma once

#include "Common/Snapshot.h"

class ModuleData;
class Object;
class Player;
class ThingTemplate;

class AoCPopulation : public Snapshot
{
public:

	AoCPopulation();

	/// Bind the owner (receives onAoCPopulationBlockChange; nullptr in unit tests) and zero everything.
	void init( Player *owner );
	/// Zero the counters and the at-cap state, without a callback (Player::init, a new game).
	void reset();

	// Template data (0 for a null template).
	static Int costOf( const ThingTemplate *tmpl );			///< AoCPopulationCost
	static Int capacityOf( const ThingTemplate *tmpl );	///< AoCPopulationCapacity
	/// The population one unit of this template takes when produced: its own cost plus the cost of the units it is created
	/// with (a transport's initial payload, the Overlord and Helix payload lists). What production checks, reserves and
	/// releases. Template data only, so the same on every machine.
	static Int productionCostOf( const ThingTemplate *tmpl );
	/// The payload part alone (what the door must create before the release).
	static Int payloadPopulationOf( const ThingTemplate *tmpl );
	/// The same sum from the parts (unit tests): own cost plus every module's payload population.
	static Int productionCostOf( Int ownCost, const ModuleData *const *modules, Int moduleCount );

	// Player hooks. The cost of an object counts while it belongs to the player; its capacity counts while it
	// belongs to the player and is not under construction; the units queued in a factory stay reserved with
	// whichever player controls the factory.
	void objectEnteringInfluence( Object *obj );
	void objectLeavingInfluence( Object *obj );
	/// The object's OBJECT_STATUS_UNDER_CONSTRUCTION bit changed while it belongs to the player.
	void objectConstructionChange( const Object *obj, Bool completed );

	// Counters. Every change re-evaluates the at-cap state; reportBlockChange passes its net edges to the owner.
	void addUsed( Int delta );
	void addCapacity( Int delta );
	void reserve( Int amount );		///< a unit was queued (cost x quantity)
	void release( Int amount );		///< a queued unit left the door or was cancelled; never below zero
	/// Recount used and capacity from the owner's objects (Player::loadPostProcess); reserved stays as loaded.
	/// The at-cap state is set without a callback.
	void recountFromOwner();
	/// The same with explicit values (tests).
	void recount( Int used, Int capacity );

	Int getUsed() const { return m_used; }
	Int getCapacity() const { return m_capacity; }
	Int getReserved() const { return m_reserved; }
	/// What production is checked against: capacity clamped to the lobby cap.
	Int getCap() const { return capFor( m_capacity, lobbyCap() ); }
	Int getHeadroom() const { return headroomFor( m_used, m_capacity, m_reserved, lobbyCap() ); }
	/// TRUE when a unit of this population fits: always for 0, else headroom >= population.
	Bool canAfford( Int population ) const;
	/// Any counter is non-zero: the player takes part in the population rules (never true with stock data).
	Bool isActive() const { return m_used != 0 || m_capacity != 0 || m_reserved != 0; }
	/// Active and no headroom left: the state whose edges reach Player::onAoCPopulationBlockChange.
	Bool isAtCap() const { return m_atCap; }
	/// Player::update, once per logic frame: an edge of the at-cap state since the last report goes to the owner. Once a
	/// frame, so a unit trading its reservation for its place in the population never shows as a pair of false edges.
	void reportBlockChange();

	// The arithmetic, shared with the unit tests.
	static Int capFor( Int capacity, Int lobbyCap );
	static Int headroomFor( Int used, Int capacity, Int reserved, Int lobbyCap );
	/// The game's lobby cap (GameLogic), or the detached value when there is no GameLogic (unit tests).
	static Int lobbyCap();
	static void setDetachedLobbyCap( Int cap );

	/// Script condition AOC_PLAYER_POPULATION_HEADROOM: headroom <comparison> amount.
	Bool scriptHeadroomIs( Int comparison, Int amount ) const;

	/// Client side of the at-cap edge, local player only: GUI:AoCNeedHousing through TheInGameUI, at most once
	/// every 120 seconds of client time. Writes UI state only; logic never reads it.
	static void showLocalWarning();

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;

private:

	void refreshBlockState();
	Int queuedPopulationOf( Object *obj ) const;

	Player *m_owner;
	Int m_used;
	Int m_capacity;
	Int m_reserved;
	Bool m_atCap;		///< derived from the counters; recomputed after a load, so not saved
	Bool m_atCapReported;	///< the state last passed to the owner; set to the loaded state after a load
	Bool m_reservedLoaded;	///< transient: a load read 'reserved' from the save (else recountFromOwner takes it from the queues)

	static Int s_detachedLobbyCap;
};
