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

// AoCCost.cpp
// Age of Commanders F1: cost vector arithmetic and INI parsing. See AoCCost.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCCost.h"
#include "AoC/AoCRules.h"
#include "Common/INI.h"

#include <string.h>

// ------------------------------------------------------------------------------------------------
void AoCCost::clear()
{
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		amount[i] = 0;
}

// ------------------------------------------------------------------------------------------------
Bool AoCCost::isZero() const
{
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		if (amount[i] != 0)
			return FALSE;
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
Bool AoCCost::hasNonCash() const
{
	for (Int i = AOC_RESOURCE_CASH + 1; i < AOC_MAX_RESOURCES; ++i)
		if (amount[i] != 0)
			return TRUE;
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
void AoCCost::add( const AoCCost &other )
{
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		amount[i] += other.amount[i];
}

// ------------------------------------------------------------------------------------------------
void AoCCost::scale( Int perMille )
{
	if (perMille < 0)
		perMille = 0;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		amount[i] = (UnsignedInt)(((UnsignedInt64)amount[i] * (UnsignedInt64)perMille) / 1000u);
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCCost::cashEquivalent() const
{
	UnsignedInt64 total = 0;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
	{
		if (amount[i] == 0)
			continue;
		total += ((UnsignedInt64)amount[i] * (UnsignedInt64)AoCRules::cashEquivalentPerMille(i)) / 1000u;
	}
	if (total > 0xFFFFFFFFu)
		total = 0xFFFFFFFFu;
	return (UnsignedInt)total;
}

// ------------------------------------------------------------------------------------------------
Bool AoCCost::operator==( const AoCCost &other ) const
{
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		if (amount[i] != other.amount[i])
			return FALSE;
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCCost::parsePair( const char *token, AoCCost &out, UnsignedInt *presentMask )
{
	if (token == nullptr)
		throw INI_INVALID_DATA;

	const char *colon = strchr(token, ':');
	if (colon == nullptr || colon == token || colon[1] == '\0')
	{
		DEBUG_CRASH(("AoCCost: expected Name:amount, got '%s'", token));
		throw INI_INVALID_DATA;
	}

	char name[64];
	size_t nameLen = (size_t)(colon - token);
	if (nameLen >= sizeof(name))
		throw INI_INVALID_DATA;
	memcpy(name, token, nameLen);
	name[nameLen] = '\0';

	Int type = AoCRules::findTypeIndex(name);
	if (type < 0)
	{
		DEBUG_CRASH(("AoCCost: '%s' is not a defined AoCResourceType", name));
		throw INI_INVALID_DATA;
	}

	const char *value = colon + 1;
	if (*value == '-')
		throw INI_INVALID_DATA;	// amounts are never negative
	out.amount[type] = INI::scanUnsignedInt(value);
	if (presentMask)
		*presentMask |= (1u << type);
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCCost::parseFromINI( INI * /*ini*/, AoCCost &out, UnsignedInt *presentMask )
{
	out.clear();
	if (presentMask)
		*presentMask = 0;
	// The default separators leave "Wood:250" whole; the INI tokenizer is static (strtok on the current line).
	for (const char *token = INI::getNextTokenOrNull(); token != nullptr; token = INI::getNextTokenOrNull())
		parsePair(token, out, presentMask);
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCCost::parseText( const char *text, AoCCost &out, UnsignedInt *presentMask )
{
	out.clear();
	if (presentMask)
		*presentMask = 0;
	if (text == nullptr)
		return;
	const char *p = text;
	while (*p)
	{
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
			++p;
		if (*p == '\0')
			break;
		const char *end = p;
		while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n')
			++end;
		char token[128];
		size_t len = (size_t)(end - p);
		if (len >= sizeof(token))
			throw INI_INVALID_DATA;
		memcpy(token, p, len);
		token[len] = '\0';
		parsePair(token, out, presentMask);
		p = end;
	}
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCCost::parseAoCCostField( INI *ini, void * /*instance*/, void *store, const void * /*userData*/ )
{
	parseFromINI(ini, *(AoCCost *)store, nullptr);
}
