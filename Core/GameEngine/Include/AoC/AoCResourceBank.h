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

// AoCResourceBank.h
// Age of Commanders F1 (design D11 section 2.1): the per-player resource bank. It lives in Player next
// to m_money and holds the non-cash amounts itself; cash (index 0) is proxied through that Money, so
// the 21 stock cash paths and the bank see one value and every stock path stays byte-identical when
// no resource types are defined. Bound to no Money (unit tests) it keeps cash in its own slot.
//
// Semantics: canAfford compares every component; withdraw of a vector with typed components is
// all-or-nothing across every component, cash included, while a cash-only vector goes through
// Money::withdraw with the stock clamp exactly as the stock sites did (callers check canAfford first,
// as stock does); deposit adds. cashEquivalent() weighs every type by its CashEquivalent per mille
// for reporting (the -aiLog cash_equivalent column, display); the stock cash checks (PLAYER_HAS_CREDITS,
// the AI poor/wealthy timers) stay cash only.

#pragma once

#include "AoC/AoCCost.h"
#include "Common/Snapshot.h"

class Money;

class AoCResourceBank : public Snapshot
{
public:

	AoCResourceBank();

	/// Bind the cash store. nullptr keeps cash inside the bank (standalone, unit tests).
	void init( Money *cash );
	/// Zero every non-cash amount (cash belongs to the owner's Money, which the owner resets).
	void reset();

	UnsignedInt count( Int type ) const;
	Bool canAfford( const AoCCost &cost ) const;
	/// Typed components present: all-or-nothing across every component, cash included. Cash only: the
	/// stock Money::withdraw clamp. TRUE when fully covered.
	Bool withdraw( const AoCCost &cost, Bool playSound = TRUE );
	/// Withdraw from one type; returns the amount actually taken (never below zero, like Money).
	UnsignedInt withdrawType( Int type, UnsignedInt amount, Bool playSound = TRUE );
	void deposit( Int type, UnsignedInt amount, Bool playSound = TRUE, Bool trackIncome = TRUE );
	void deposit( const AoCCost &amounts, Bool playSound = TRUE, Bool trackIncome = TRUE );
	/// Scripts: set one type to an exact amount (cash: silent, like PLAYER_SET_MONEY).
	void setAmount( Int type, UnsignedInt amount );

	/// Every type weighted by its CashEquivalent per mille; equals countMoney() when the feature is off.
	UnsignedInt cashEquivalent() const;
	/// out = what is missing per type; TRUE if anything is missing (tooltips, AI reasons).
	Bool shortfall( const AoCCost &cost, AoCCost &out ) const;

	/// Player death: every non-cash amount to zero (the stock line zeroes cash).
	void clearNonCash();
	/// Asset transfer: move every non-cash amount from 'that' to this bank (the stock lines move cash).
	void transferNonCashFrom( AoCResourceBank &that );

	// The script handlers after their player lookup (ScriptConditions AOC_PLAYER_HAS_RESOURCE, ScriptActions
	// AOC_PLAYER_GIVE_RESOURCE and AOC_PLAYER_SET_RESOURCE), here so the unit tests run the same code. The
	// type is named as in the script; an unknown name is FALSE or no effect, never an error.
	/// amount <comparison> count(type), the PLAYER_HAS_CREDITS orientation.
	Bool scriptHasResource( const char *typeName, Int comparison, Int amount ) const;
	/// A positive amount deposits, a negative one withdraws (clamped at zero, like Money).
	void scriptGiveResource( const char *typeName, Int amount );
	/// Sets the type to the amount; a negative amount sets zero.
	void scriptSetResource( const char *typeName, Int amount );

	Money *getCash() { return m_cash; }

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;

private:

	Money *m_cash;												///< the owner's Money, index 0 (nullptr: standalone)
	UnsignedInt m_amount[AOC_MAX_RESOURCES];	///< indices 1 and up; [0] used only when standalone
};
