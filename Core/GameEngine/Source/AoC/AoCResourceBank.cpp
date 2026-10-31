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

// AoCResourceBank.cpp
// Age of Commanders F1: the per-player resource bank. See AoCResourceBank.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCResourceBank.h"
#include "AoC/AoCRules.h"
#include "Common/Money.h"
#include "Common/Xfer.h"

// ------------------------------------------------------------------------------------------------
AoCResourceBank::AoCResourceBank()
{
	m_cash = nullptr;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		m_amount[i] = 0;
}

// ------------------------------------------------------------------------------------------------
void AoCResourceBank::init( Money *cash )
{
	m_cash = cash;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		m_amount[i] = 0;
}

// ------------------------------------------------------------------------------------------------
void AoCResourceBank::reset()
{
	for (Int i = AOC_RESOURCE_CASH + 1; i < AOC_MAX_RESOURCES; ++i)
		m_amount[i] = 0;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCResourceBank::count( Int type ) const
{
	if (type < 0 || type >= AOC_MAX_RESOURCES)
		return 0;
	if (type == AOC_RESOURCE_CASH && m_cash)
		return m_cash->countMoney();
	return m_amount[type];
}

// ------------------------------------------------------------------------------------------------
Bool AoCResourceBank::canAfford( const AoCCost &cost ) const
{
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		if (cost.amount[i] > count(i))
			return FALSE;
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
Bool AoCResourceBank::withdraw( const AoCCost &cost, Bool playSound )
{
	// A cash-only vector is the stock payment: Money::withdraw with the stock clamp (Money never goes
	// into debt). This is every payment while the feature is off.
	if (!cost.hasNonCash())
		return withdrawType(AOC_RESOURCE_CASH, cost.amount[AOC_RESOURCE_CASH], playSound) == cost.amount[AOC_RESOURCE_CASH];

	// A vector with typed components is all or nothing across every component, cash included: one
	// short component leaves the bank untouched.
	if (!canAfford(cost))
		return FALSE;

	withdrawType(AOC_RESOURCE_CASH, cost.amount[AOC_RESOURCE_CASH], playSound);
	for (Int i = AOC_RESOURCE_CASH + 1; i < AOC_MAX_RESOURCES; ++i)
		m_amount[i] -= cost.amount[i];

	return TRUE;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCResourceBank::withdrawType( Int type, UnsignedInt amount, Bool playSound )
{
	if (type < 0 || type >= AOC_MAX_RESOURCES)
		return 0;
	if (type == AOC_RESOURCE_CASH && m_cash)
		return m_cash->withdraw(amount, playSound);
	if (amount > m_amount[type])
		amount = m_amount[type];
	m_amount[type] -= amount;
	return amount;
}

// ------------------------------------------------------------------------------------------------
void AoCResourceBank::deposit( Int type, UnsignedInt amount, Bool playSound, Bool trackIncome )
{
	if (type < 0 || type >= AOC_MAX_RESOURCES)
		return;
	if (type == AOC_RESOURCE_CASH && m_cash)
	{
		m_cash->deposit(amount, playSound, trackIncome);
		return;
	}
	m_amount[type] += amount;
}

// ------------------------------------------------------------------------------------------------
void AoCResourceBank::deposit( const AoCCost &amounts, Bool playSound, Bool trackIncome )
{
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		deposit(i, amounts.amount[i], playSound, trackIncome);
}

// ------------------------------------------------------------------------------------------------
void AoCResourceBank::setAmount( Int type, UnsignedInt amount )
{
	if (type < 0 || type >= AOC_MAX_RESOURCES)
		return;
	if (type == AOC_RESOURCE_CASH && m_cash)
	{
		// Same steps as the stock PLAYER_SET_MONEY action.
		m_cash->withdraw(m_cash->countMoney());
		m_cash->deposit(amount, FALSE, FALSE);
		return;
	}
	m_amount[type] = amount;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCResourceBank::cashEquivalent() const
{
	const Int typeCount = AoCRules::typeCount();
	UnsignedInt64 total = 0;
	for (Int i = 0; i < typeCount && i < AOC_MAX_RESOURCES; ++i)
	{
		const UnsignedInt have = count(i);
		if (have == 0)
			continue;
		total += ((UnsignedInt64)have * (UnsignedInt64)AoCRules::cashEquivalentPerMille(i)) / 1000u;
	}
	if (total > 0xFFFFFFFFu)
		total = 0xFFFFFFFFu;
	return (UnsignedInt)total;
}

// ------------------------------------------------------------------------------------------------
Bool AoCResourceBank::shortfall( const AoCCost &cost, AoCCost &out ) const
{
	Bool any = FALSE;
	out.clear();
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
	{
		const UnsignedInt have = count(i);
		if (cost.amount[i] > have)
		{
			out.amount[i] = cost.amount[i] - have;
			any = TRUE;
		}
	}
	return any;
}

// ------------------------------------------------------------------------------------------------
void AoCResourceBank::clearNonCash()
{
	reset();
}

// ------------------------------------------------------------------------------------------------
void AoCResourceBank::transferNonCashFrom( AoCResourceBank &that )
{
	if (&that == this)
		return;
	for (Int i = AOC_RESOURCE_CASH + 1; i < AOC_MAX_RESOURCES; ++i)
	{
		m_amount[i] += that.m_amount[i];
		that.m_amount[i] = 0;
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC: every amount, cash included, once resource types are defined. Nothing when the feature is
	* off, so stock per-frame CRCs are unchanged (the stock Money::crc is empty). */
// ------------------------------------------------------------------------------------------------
void AoCResourceBank::crc( Xfer *xfer )
{
	if (!AoCRules::enabled())
		return;
	const Int typeCount = AoCRules::typeCount();
	for (Int i = 0; i < typeCount && i < AOC_MAX_RESOURCES; ++i)
	{
		UnsignedInt value = count(i);
		xfer->xferUnsignedInt( &value );
	}
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version: a count byte and the non-cash amounts (cash is the owner's Money, saved by
	*    the stock code). Loading maps the saved indices through TheAoCRules, which xfers first.
	*/
// ------------------------------------------------------------------------------------------------
void AoCResourceBank::xfer( Xfer *xfer )
{
	const XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	const Bool loading = (xfer->getXferMode() == XFER_LOAD);

	UnsignedByte count = (UnsignedByte)AoCRules::typeCount();
	xfer->xferUnsignedByte( &count );

	if (loading)
		reset();

	for (Int i = AOC_RESOURCE_CASH + 1; i < (Int)count; ++i)
	{
		UnsignedInt value = (!loading && i < AOC_MAX_RESOURCES) ? m_amount[i] : 0;
		xfer->xferUnsignedInt( &value );
		if (loading)
		{
			const Int type = TheAoCRules ? TheAoCRules->remapLoadedType(i) : -1;
			if (type > AOC_RESOURCE_CASH && type < AOC_MAX_RESOURCES)
				m_amount[type] = value;
		}
	}
}

// ------------------------------------------------------------------------------------------------
void AoCResourceBank::loadPostProcess()
{
}
