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

// AoCCost.h
// Age of Commanders F1 (design D11 section 2.1): a cost vector over the resource types defined by
// TheAoCRules. Index 0 is always cash, so a vector whose other components are zero is exactly a
// stock cash cost. Amounts are integers (determinism checklist, D11 1.2).
//
// INI syntax, free order, any subset of the defined type names ("Wood:250 Cash:100"):
//
//   Object AmericaBarracks
//     BuildCost = 600          ; stock key, cash
//     AoCCost   = Wood:250     ; added types; Cash:n here writes BuildCost (one cash store)
//   End
//
// A name that is not a defined AoCResourceType is an INI error (INI_INVALID_DATA), like an unknown KindOf.

#pragma once

#include "Lib/BaseType.h"

class INI;

enum
{
	AOC_MAX_RESOURCES = 8,	///< hard cap on resource types, also the save-game array size
	AOC_RESOURCE_CASH = 0,	///< the cash type is always index 0 (the stock Money)
};

struct AoCCost
{
	UnsignedInt amount[AOC_MAX_RESOURCES];

	AoCCost() { clear(); }

	void clear();
	Bool isZero() const;
	/// TRUE when any component other than cash is non-zero: the test every hook uses to stay on the stock path.
	Bool hasNonCash() const;
	void add( const AoCCost &other );
	/// amount[i] = amount[i] * perMille / 1000 for every component, integer arithmetic.
	void scale( Int perMille );
	/// Sum of every component weighted by its type's CashEquivalent (per mille); cash counts 1:1.
	UnsignedInt cashEquivalent() const;

	Bool operator==( const AoCCost &other ) const;
	Bool operator!=( const AoCCost &other ) const { return !(*this == other); }

	/**
		Parse the rest of the current INI line as "Name:amount" tokens into 'out' (cleared first).
		presentMask, when given, gets bit i set for every type named, so a caller can tell "Cash:0" from
		an absent cash component. Throws INI_INVALID_DATA on an unknown name, a bad amount or a bad token.
	*/
	static void parseFromINI( INI *ini, AoCCost &out, UnsignedInt *presentMask = nullptr );
	/// Same grammar on a whitespace-separated string (unit tests and tools).
	static void parseText( const char *text, AoCCost &out, UnsignedInt *presentMask = nullptr );
	/// One "Name:amount" token.
	static void parsePair( const char *token, AoCCost &out, UnsignedInt *presentMask );

	/// FieldParse procedure: 'store' points at an AoCCost. The instance is ignored.
	static void parseAoCCostField( INI *ini, void *instance, void *store, const void *userData );
};
