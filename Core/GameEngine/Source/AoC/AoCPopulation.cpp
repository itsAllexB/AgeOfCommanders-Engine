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

// AoCPopulation.cpp
// Age of Commanders F4: the per-player population. See AoCPopulation.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCPopulation.h"
#include "AoC/AoCRules.h"
#include "Common/Module.h"
#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/GameText.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/ProductionUpdate.h"

#include <chrono>

Int AoCPopulation::s_detachedLobbyCap = 0;

namespace
{

// The warning repeats at most this often, in client (wall clock) time. Client state only.
const long long AOC_POPULATION_WARNING_INTERVAL_MS = 120 * 1000;
Bool s_warningShown = FALSE;				///< client state: a warning was shown in this game
long long s_warningLastShownMs = 0;	///< client state: when

struct PopulationCount
{
	Int used;
	Int capacity;
	Int reserved;
};

}

// ------------------------------------------------------------------------------------------------
AoCPopulation::AoCPopulation()
{
	m_owner = nullptr;
	m_used = 0;
	m_capacity = 0;
	m_reserved = 0;
	m_atCap = FALSE;
	m_atCapReported = FALSE;
	m_reservedLoaded = FALSE;
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::init( Player *owner )
{
	m_owner = owner;
	reset();
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::reset()
{
	m_used = 0;
	m_capacity = 0;
	m_reserved = 0;
	m_atCap = FALSE;
	m_atCapReported = FALSE;
	m_reservedLoaded = FALSE;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCPopulation::costOf( const ThingTemplate *tmpl )
{
	return tmpl ? (Int)tmpl->getAoCPopulationCost() : 0;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCPopulation::capacityOf( const ThingTemplate *tmpl )
{
	return tmpl ? (Int)tmpl->getAoCPopulationCapacity() : 0;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCPopulation::payloadPopulationOf( const ThingTemplate *tmpl )
{
	if (tmpl == nullptr)
		return 0;
	const ModuleInfo &modules = tmpl->getBehaviorModuleInfo();
	Int total = 0;
	for (Int i = 0; i < modules.getCount(); ++i)
	{
		const ModuleData *data = modules.getNthData(i);
		if (data)
			total += data->getAoCInitialPayloadPopulation();
	}
	return total;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCPopulation::productionCostOf( const ThingTemplate *tmpl )
{
	return costOf(tmpl) + payloadPopulationOf(tmpl);
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCPopulation::productionCostOf( Int ownCost, const ModuleData *const *modules, Int moduleCount )
{
	Int total = ownCost;
	for (Int i = 0; i < moduleCount; ++i)
	{
		if (modules[i])
			total += modules[i]->getAoCInitialPayloadPopulation();
	}
	return total;
}

// ------------------------------------------------------------------------------------------------
/** The population still reserved by the unit entries queued in this object's factory: each entry
	* reserved its population times its quantity when it was queued and releases one unit's worth as
	* each unit leaves the door. Modules are not ready while the object is being constructed. */
// ------------------------------------------------------------------------------------------------
Int AoCPopulation::queuedPopulationOf( Object *obj ) const
{
	if (obj == nullptr || !obj->areModulesReady())
		return 0;
	ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
	if (pu == nullptr)
		return 0;
	Int total = 0;
	for (const ProductionEntry *entry = pu->firstProduction(); entry; entry = pu->nextProduction(entry))
	{
		if (entry->getProductionType() == PRODUCTION_UNIT)
			total += productionCostOf(entry->getProductionObject()) * entry->getProductionQuantityRemaining();
	}
	return total;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCPopulation::livingCostOf( const Object *obj )
{
	if (obj == nullptr || obj->isAoCPopulationFreed())
		return 0;
	return costOf(obj->getTemplate());
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCPopulation::livingCapacityOf( const Object *obj )
{
	if (obj == nullptr || obj->isAoCPopulationFreed() || obj->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION))
		return 0;
	return capacityOf(obj->getTemplate());
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::objectEnteringInfluence( Object *obj )
{
	if (obj == nullptr)
		return;
	const Int cost = livingCostOf(obj);
	const Int capacity = livingCapacityOf(obj);
	// A factory that changes hands with units in its queue takes their reservation along (captures cancel
	// the queue first, so this covers script transfers). Nothing is queued while an object is created.
	const Int queued = queuedPopulationOf(obj);
	if (cost == 0 && capacity == 0 && queued == 0)
		return;	// every stock object

	m_used += cost;
	m_capacity += capacity;
	m_reserved += queued;
	refreshBlockState();
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::objectLeavingInfluence( Object *obj )
{
	if (obj == nullptr)
		return;
	const Int cost = livingCostOf(obj);
	const Int capacity = livingCapacityOf(obj);
	// Death, sale and capture empty the queue before the object leaves; an object removed without dying
	// (a script, a replacement upgrade) still frees what its queue reserved. Nothing to look for when the
	// player has nothing reserved.
	const Int queued = (m_reserved > 0) ? queuedPopulationOf(obj) : 0;
	if (cost == 0 && capacity == 0 && queued == 0)
		return;	// every stock object

	m_used -= cost;
	m_capacity -= capacity;
	m_reserved -= queued;
	DEBUG_ASSERTCRASH(m_used >= 0 && m_capacity >= 0, ("AoCPopulation: used %d or capacity %d went below zero", m_used, m_capacity));
	if (m_reserved < 0)
	{
		DEBUG_CRASH(("AoCPopulation: reserved population went below zero"));
		m_reserved = 0;
	}
	refreshBlockState();
}

// ------------------------------------------------------------------------------------------------
/** Team::setControllingPlayer re-points a prototype's owner, which changes the owner of every
	* instance's members without a team change (no becomingTeamMember). Review of PR #5, finding 2. */
// ------------------------------------------------------------------------------------------------
void AoCPopulation::teamPrototypeLeaving( TeamPrototype *proto )
{
	if (proto == nullptr)
		return;
	for (DLINK_ITERATOR<Team> team = proto->iterate_TeamInstanceList(); !team.done(); team.advance())
	{
		if (team.cur() == nullptr)
			continue;
		for (DLINK_ITERATOR<Object> member = team.cur()->iterate_TeamMemberList(); !member.done(); member.advance())
			objectLeavingInfluence(member.cur());
	}
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::teamPrototypeEntering( TeamPrototype *proto )
{
	if (proto == nullptr)
		return;
	for (DLINK_ITERATOR<Team> team = proto->iterate_TeamInstanceList(); !team.done(); team.advance())
	{
		if (team.cur() == nullptr)
			continue;
		for (DLINK_ITERATOR<Object> member = team.cur()->iterate_TeamMemberList(); !member.done(); member.advance())
			objectEnteringInfluence(member.cur());
	}
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::objectConstructionChange( const Object *obj, Bool completed )
{
	if (obj == nullptr || obj->isAoCPopulationFreed())
		return;
	const Int capacity = capacityOf(obj->getTemplate());
	if (capacity == 0)
		return;
	addCapacity(completed ? capacity : -capacity);
}

// ------------------------------------------------------------------------------------------------
/** Review of PR #5, finding 3 (decision: free the slot at death). Stock removes a dead unit's body
	* seconds later (DestructionDelay), and the leave at removal used to be when its population went. */
// ------------------------------------------------------------------------------------------------
void AoCPopulation::objectDying( Object *obj )
{
	if (obj == nullptr || obj->isAoCPopulationFreed())
		return;
	const Int cost = livingCostOf(obj);
	const Int capacity = livingCapacityOf(obj);
	if (cost == 0 && capacity == 0)
		return;	// every stock object: no marker, nothing in the CRC
	obj->friend_setAoCPopulationFreed();
	m_used -= cost;
	m_capacity -= capacity;
	DEBUG_ASSERTCRASH(m_used >= 0 && m_capacity >= 0, ("AoCPopulation: used %d or capacity %d went below zero", m_used, m_capacity));
	refreshBlockState();
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::addUsed( Int delta )
{
	if (delta == 0)
		return;
	m_used += delta;
	refreshBlockState();
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::addCapacity( Int delta )
{
	if (delta == 0)
		return;
	m_capacity += delta;
	DEBUG_ASSERTCRASH(m_capacity >= 0, ("AoCPopulation: capacity %d went below zero", m_capacity));
	refreshBlockState();
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::reserve( Int amount )
{
	if (amount <= 0)
		return;
	m_reserved += amount;
	refreshBlockState();
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::release( Int amount )
{
	if (amount <= 0)
		return;
	if (amount > m_reserved)
	{
		// Should not happen: every queued unit entry holds its reservation (a save without the population
		// block takes it from the queues). Never below zero.
		DEBUG_CRASH(("AoCPopulation: releasing %d population with only %d reserved", amount, m_reserved));
		amount = m_reserved;
	}
	m_reserved -= amount;
	refreshBlockState();
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::recount( Int used, Int capacity )
{
	m_used = used;
	m_capacity = capacity;
	refreshBlockState();
	m_atCapReported = m_atCap;
}

// ------------------------------------------------------------------------------------------------
static void countPopulation( Object *obj, void *userData )
{
	PopulationCount *count = (PopulationCount *)userData;
	count->used += AoCPopulation::livingCostOf(obj);
	count->capacity += AoCPopulation::livingCapacityOf(obj);
	if (obj->areModulesReady())
	{
		ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
		if (pu)
		{
			for (const ProductionEntry *entry = pu->firstProduction(); entry; entry = pu->nextProduction(entry))
			{
				if (entry->getProductionType() == PRODUCTION_UNIT)
					count->reserved += AoCPopulation::productionCostOf(entry->getProductionObject()) * entry->getProductionQuantityRemaining();
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
/** After a load: used and capacity are recounted from the objects the owner controls, so a save made
	* before F4 (or with other data) heals itself. Reserved stays as saved; a save without the population
	* block takes it from the production queues instead. */
// ------------------------------------------------------------------------------------------------
void AoCPopulation::recountFromOwner()
{
	if (m_owner == nullptr)
		return;
	PopulationCount count;
	count.used = 0;
	count.capacity = 0;
	count.reserved = 0;
	m_owner->iterateObjects(countPopulation, &count);
	applyRecount(count.used, count.capacity, count.reserved);
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::applyRecount( Int used, Int capacity, Int queuedReserved )
{
	if (!m_reservedLoaded)
		m_reserved = queuedReserved;
	m_reservedLoaded = FALSE;
	recount(used, capacity);
}

// ------------------------------------------------------------------------------------------------
Bool AoCPopulation::canAfford( Int population ) const
{
	if (population <= 0)
		return TRUE;
	return getHeadroom() >= population;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCPopulation::capFor( Int capacity, Int lobbyCap )
{
	if (isUnlimited(lobbyCap))
		return AOC_POPULATION_CAP_UNLIMITED;	// AoC F7: no cap at all
	if (lobbyCap > 0 && lobbyCap < capacity)
		return lobbyCap;
	return capacity;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCPopulation::headroomFor( Int used, Int capacity, Int reserved, Int lobbyCap )
{
	if (isUnlimited(lobbyCap))
		return AOC_POPULATION_UNLIMITED_HEADROOM;	// AoC F7: the check is off; canAfford is TRUE and nobody is at cap
	return capFor(capacity, lobbyCap) - used - reserved;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCPopulation::lobbyCap()
{
	return TheGameLogic ? TheGameLogic->getAoCPopulationCap() : s_detachedLobbyCap;
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCPopulation::setDetachedLobbyCap( Int cap )
{
	s_detachedLobbyCap = cap;
}

// ------------------------------------------------------------------------------------------------
Bool AoCPopulation::scriptHeadroomIs( Int comparison, Int amount ) const
{
	return AoCRules::compareInts(comparison, getHeadroom(), amount);
}

// ------------------------------------------------------------------------------------------------
/** "At cap" is an active player with no headroom left. */
// ------------------------------------------------------------------------------------------------
void AoCPopulation::refreshBlockState()
{
	m_atCap = isActive() && getHeadroom() <= 0;
}

// ------------------------------------------------------------------------------------------------
/** The edges of the at-cap state go to the owner, which mirrors Player::onPowerBrownOutChange; the
	* logic itself changes nothing on an edge, production simply waits through canMakeUnit. */
// ------------------------------------------------------------------------------------------------
void AoCPopulation::reportBlockChange()
{
	if (m_atCap == m_atCapReported)
		return;
	m_atCapReported = m_atCap;
	if (m_owner)
		m_owner->onAoCPopulationBlockChange(m_atCap);
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCPopulation::showLocalWarning()
{
	if (TheInGameUI == nullptr || TheGameText == nullptr)
		return;
	const long long nowMs = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	if (s_warningShown && nowMs - s_warningLastShownMs < AOC_POPULATION_WARNING_INTERVAL_MS)
		return;
	s_warningShown = TRUE;
	s_warningLastShownMs = nowMs;
	TheInGameUI->messageNoFormat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:AoCNeedHousing", L"Population limit reached. Build houses."));
}

// ------------------------------------------------------------------------------------------------
/** Review of PR #5, nit 5: the throttle used to carry over, so a new game started within 120 s of the
	* last warning stayed silent the first time it reached the cap. Client state only. */
// ------------------------------------------------------------------------------------------------
/*static*/ void AoCPopulation::resetLocalWarning()
{
	s_warningShown = FALSE;
	s_warningLastShownMs = 0;
}

// ------------------------------------------------------------------------------------------------
/** CRC: the three counters and the lobby cap once the player takes part in the population rules.
	* Nothing while every counter is zero, which is always the case with stock data, so stock per-frame
	* CRCs are unchanged. */
// ------------------------------------------------------------------------------------------------
void AoCPopulation::crc( Xfer *xfer )
{
	if (!isActive())
		return;
	xfer->xferInt( &m_used );
	xfer->xferInt( &m_capacity );
	xfer->xferInt( &m_reserved );
	Int cap = lobbyCap();
	xfer->xferInt( &cap );
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version: used, capacity, reserved. The at-cap state is derived and recomputed, and
	*    Player::loadPostProcess recounts used and capacity from the objects.
	*/
// ------------------------------------------------------------------------------------------------
void AoCPopulation::xfer( Xfer *xfer )
{
	const XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	xfer->xferInt( &m_used );
	xfer->xferInt( &m_capacity );
	xfer->xferInt( &m_reserved );

	if (xfer->getXferMode() == XFER_LOAD)
	{
		m_reservedLoaded = TRUE;
		refreshBlockState();
		m_atCapReported = m_atCap;
	}
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::loadPostProcess()
{
}
