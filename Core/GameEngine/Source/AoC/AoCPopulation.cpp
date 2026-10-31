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
#include "Common/Player.h"
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
			total += costOf(entry->getProductionObject()) * entry->getProductionQuantityRemaining();
	}
	return total;
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::objectEnteringInfluence( Object *obj )
{
	if (obj == nullptr)
		return;
	const ThingTemplate *tmpl = obj->getTemplate();
	const Int cost = costOf(tmpl);
	const Int capacity = obj->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION) ? 0 : capacityOf(tmpl);
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
	const ThingTemplate *tmpl = obj->getTemplate();
	const Int cost = costOf(tmpl);
	const Int capacity = obj->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION) ? 0 : capacityOf(tmpl);
	// Death, sale and capture empty the queue before the object leaves; an object removed without dying
	// (a script, a replacement upgrade) still frees what its queue reserved. Nothing to look for when the
	// player has nothing reserved.
	const Int queued = (m_reserved > 0) ? queuedPopulationOf(obj) : 0;
	if (cost == 0 && capacity == 0 && queued == 0)
		return;	// every stock object

	m_used -= cost;
	m_capacity -= capacity;
	m_reserved -= queued;
	if (m_reserved < 0)
	{
		DEBUG_CRASH(("AoCPopulation: reserved population went below zero"));
		m_reserved = 0;
	}
	refreshBlockState();
}

// ------------------------------------------------------------------------------------------------
void AoCPopulation::objectConstructionChange( const Object *obj, Bool completed )
{
	if (obj == nullptr)
		return;
	const Int capacity = capacityOf(obj->getTemplate());
	if (capacity == 0)
		return;
	addCapacity(completed ? capacity : -capacity);
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
	const ThingTemplate *tmpl = obj->getTemplate();
	count->used += AoCPopulation::costOf(tmpl);
	if (!obj->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION))
		count->capacity += AoCPopulation::capacityOf(tmpl);
	if (obj->areModulesReady())
	{
		ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
		if (pu)
		{
			for (const ProductionEntry *entry = pu->firstProduction(); entry; entry = pu->nextProduction(entry))
			{
				if (entry->getProductionType() == PRODUCTION_UNIT)
					count->reserved += AoCPopulation::costOf(entry->getProductionObject()) * entry->getProductionQuantityRemaining();
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
	if (!m_reservedLoaded)
		m_reserved = count.reserved;
	m_reservedLoaded = FALSE;
	recount(count.used, count.capacity);
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
	if (lobbyCap > 0 && lobbyCap < capacity)
		return lobbyCap;
	return capacity;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCPopulation::headroomFor( Int used, Int capacity, Int reserved, Int lobbyCap )
{
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
	static Bool s_shown = FALSE;
	static long long s_lastShownMs = 0;
	const long long nowMs = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	if (s_shown && nowMs - s_lastShownMs < AOC_POPULATION_WARNING_INTERVAL_MS)
		return;
	s_shown = TRUE;
	s_lastShownMs = nowMs;
	TheInGameUI->messageNoFormat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:AoCNeedHousing", L"Population limit reached. Build houses."));
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
