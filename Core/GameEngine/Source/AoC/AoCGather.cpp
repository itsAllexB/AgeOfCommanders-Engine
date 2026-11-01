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

// AoCGather.cpp
// Age of Commanders F2: typed gathering, the parts the stock supply modules share. See AoCGather.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCGather.h"
#include "AoC/AoCHarness.h"
#include "AoC/AoCRules.h"
#include "Common/GameCommon.h"
#include "Common/INI.h"
#include "Common/Xfer.h"
#include "GameClient/GameText.h"
#include "GameLogic/GameLogic.h"
#include "WWMath/wwmath.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------
AoCCargoData::AoCCargoData()
{
	cargoMask = AoCGather::typeBit(AOC_RESOURCE_CASH);
	maxUnitsMask = 0;
	gatherDelayMask = 0;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
	{
		maxUnits[i] = 0;
		gatherDelayFrames[i] = 0;
	}
}

// ------------------------------------------------------------------------------------------------
Bool AoCCargoData::canCarry( Int type ) const
{
	return (cargoMask & AoCGather::typeBit(type)) != 0;
}

// ------------------------------------------------------------------------------------------------
Int AoCCargoData::maxUnitsFor( Int type, Int stockMaxBoxes ) const
{
	if (maxUnitsMask & AoCGather::typeBit(type))
		return (Int)maxUnits[type];
	return stockMaxBoxes;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCCargoData::gatherDelayFor( Int type, UnsignedInt stockDelayFrames ) const
{
	if (gatherDelayMask & AoCGather::typeBit(type))
		return gatherDelayFrames[type];
	return stockDelayFrames;
}

// ------------------------------------------------------------------------------------------------
void AoCCargoData::setMaxUnits( const AoCCost &units, UnsignedInt presentMask )
{
	// A capacity of 0 for a type the gatherer may carry would let it dock and fail the pick-up forever (review F2,
	// finding 5): refuse it at parse time, as an unknown type name is refused.
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
	{
		if ((presentMask & AoCGather::typeBit(i)) && units.amount[i] == 0)
		{
			DEBUG_CRASH(("AoCMaxUnits: the capacity of %s must be at least 1", AoCGather::typeName(i)));
			throw INI_INVALID_DATA;
		}
	}
	maxUnitsMask = presentMask;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		maxUnits[i] = (presentMask & AoCGather::typeBit(i)) ? units.amount[i] : 0;
}

// ------------------------------------------------------------------------------------------------
void AoCCargoData::setGatherDelayMsecs( const AoCCost &msecs, UnsignedInt presentMask )
{
	gatherDelayMask = presentMask;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		gatherDelayFrames[i] = (presentMask & AoCGather::typeBit(i)) ? AoCGather::msecsToFrames(msecs.amount[i]) : 0;
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCCargoData::parseCargoTypes( INI *ini, void *instance, void *store, const void *userData )
{
	UnsignedInt mask = 0;
	AoCGather::parseResourceTypeList(ini, instance, &mask, userData);
	((AoCCargoData *)store)->setCargoTypes(mask);
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCCargoData::parseMaxUnits( INI *ini, void * /*instance*/, void *store, const void * /*userData*/ )
{
	AoCCost units;
	UnsignedInt present = 0;
	AoCCost::parseFromINI(ini, units, &present);
	((AoCCargoData *)store)->setMaxUnits(units, present);
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCCargoData::parseGatherDelay( INI *ini, void * /*instance*/, void *store, const void * /*userData*/ )
{
	AoCCost msecs;
	UnsignedInt present = 0;
	AoCCost::parseFromINI(ini, msecs, &present);
	((AoCCargoData *)store)->setGatherDelayMsecs(msecs, present);
}

// ------------------------------------------------------------------------------------------------
AoCCargoState::AoCCargoState()
{
	cargoType = AOC_RESOURCE_CASH;
	assignedType = -1;
}

// ------------------------------------------------------------------------------------------------
void AoCCargoState::xfer( Xfer *xfer )
{
	Int cargo = cargoType;
	Int assigned = assignedType;
	xfer->xferInt( &cargo );
	xfer->xferInt( &assigned );
	if (xfer->getXferMode() == XFER_LOAD)
	{
		Int mappedCargo = TheAoCRules ? TheAoCRules->remapLoadedType(cargo) : (cargo == AOC_RESOURCE_CASH ? cargo : -1);
		cargoType = (mappedCargo >= 0) ? mappedCargo : AOC_RESOURCE_CASH;
		Int mappedAssigned = -1;
		if (assigned >= 0)
			mappedAssigned = TheAoCRules ? TheAoCRules->remapLoadedType(assigned) : (assigned == AOC_RESOURCE_CASH ? assigned : -1);
		assignedType = mappedAssigned;
	}
}

// ------------------------------------------------------------------------------------------------
void AoCCargoState::xferSince( Xfer *xfer, UnsignedByte blockVersion, UnsignedByte sinceVersion )
{
	if (blockVersion >= sinceVersion)
		this->xfer( xfer );
}

// ------------------------------------------------------------------------------------------------
void AoCCargoState::crc( Xfer *xfer )
{
	if (!AoCRules::enabled())
		return;
	Int cargo = cargoType;
	Int assigned = assignedType;
	xfer->xferInt( &cargo );
	xfer->xferInt( &assigned );
}

// ------------------------------------------------------------------------------------------------
Bool AoCGather::mustDropBeforePickup( Int cargoType, Int cargoCount, Int nodeType )
{
	return cargoCount > 0 && cargoType != nodeType;
}

// ------------------------------------------------------------------------------------------------
Bool AoCGather::wantsNodeType( Int nodeType, Int assignedType, UnsignedInt cargoMask )
{
	if (assignedType >= 0)
		return nodeType == assignedType;
	return (cargoMask & typeBit(nodeType)) != 0;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCGather::scaleDelayByRate( UnsignedInt delayFrames, Int ratePerMille )
{
	if (ratePerMille <= 0 || ratePerMille == 1000)
		return delayFrames;
	return (UnsignedInt)(((UnsignedInt64)delayFrames * 1000u) / (UnsignedInt64)ratePerMille);
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCGather::gatherDelayFrames( const AoCCargoData &cargo, Int nodeType, UnsignedInt stockDelayFrames, const Player * /*owner*/ )
{
	const UnsignedInt delay = cargo.gatherDelayFor( nodeType, stockDelayFrames );
	// F13's multiplication point: the owner's gather rate for this type, per mille (Player::m_aocGatherPerMille[type]
	// from AoCGatherRateUpgrade, times the horde bonus), scales the delay here and nowhere else. 1000 until F13.
	const Int gatherRatePerMille = 1000;
	return scaleDelayByRate( delay, gatherRatePerMille );
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCGather::msecsToFrames( UnsignedInt msecs )
{
	return (UnsignedInt)WWMath::Ceil(ConvertDurationFromMsecsToFrames((Real)msecs));
}

// ------------------------------------------------------------------------------------------------
Int AoCGather::parseTypeName( const char *name )
{
	Int type = name ? AoCRules::findTypeIndex(name) : -1;
	if (type < 0)
	{
		DEBUG_CRASH(("AoCGather: '%s' is not a defined AoCResourceType", name ? name : ""));
		throw INI_INVALID_DATA;
	}
	return type;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCGather::parseTypeListText( const char *text )
{
	UnsignedInt mask = 0;
	if (text == nullptr)
		return mask;
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
		char token[64];
		size_t len = (size_t)(end - p);
		if (len >= sizeof(token))
			throw INI_INVALID_DATA;
		memcpy(token, p, len);
		token[len] = '\0';
		mask |= typeBit(parseTypeName(token));
		p = end;
	}
	return mask;
}

// ------------------------------------------------------------------------------------------------
void AoCGather::parseResourceType( INI * /*ini*/, void * /*instance*/, void *store, const void * /*userData*/ )
{
	*(Int *)store = parseTypeName(INI::getNextToken());
}

// ------------------------------------------------------------------------------------------------
void AoCGather::parseResourceTypeList( INI * /*ini*/, void * /*instance*/, void *store, const void * /*userData*/ )
{
	// The INI tokenizer is static (strtok on the current line), as in AoCCost::parseFromINI.
	UnsignedInt mask = 0;
	for (const char *token = INI::getNextTokenOrNull(); token != nullptr; token = INI::getNextTokenOrNull())
		mask |= typeBit(parseTypeName(token));
	if (mask == 0)
		throw INI_INVALID_DATA;	// an empty list would make a node or gatherer that can never be used
	*(UnsignedInt *)store = mask;
}

// ------------------------------------------------------------------------------------------------
void AoCGather::parsePositiveUnsignedInt( INI * /*ini*/, void * /*instance*/, void *store, const void * /*userData*/ )
{
	// A negative value would wrap into a huge deposit per unit (review F2, finding 2), and 0 would make a drop-off that
	// takes a type and pays nothing: both are refused like a bad type name, the INI loader names the file and line.
	const char *token = INI::getNextToken();
	while (*token == ' ' || *token == '\t')
		++token;
	if (*token == '-')
	{
		DEBUG_CRASH(("AoCGather: '%s' must be a whole number of at least 1", token));
		throw INI_INVALID_DATA;
	}
	const UnsignedInt value = INI::scanUnsignedInt(token);
	if (value == 0)
	{
		DEBUG_CRASH(("AoCGather: '%s' must be at least 1", token));
		throw INI_INVALID_DATA;
	}
	*(UnsignedInt *)store = value;
}

// ------------------------------------------------------------------------------------------------
const char *AoCGather::typeName( Int type )
{
	if (TheAoCRules == nullptr)
		return type == AOC_RESOURCE_CASH ? "Cash" : "?";
	if (type < 0 || type >= TheAoCRules->getTypeCount())
		return "?";
	return TheAoCRules->getTypeName(type).str();
}

// ------------------------------------------------------------------------------------------------
UnicodeString AoCGather::depositText( Int type, UnsignedInt amount )
{
	UnicodeString label;
	Bool exists = FALSE;
	if (TheAoCRules && TheGameText && TheAoCRules->getDisplayLabel(type).isNotEmpty())
		label = TheGameText->fetch(TheAoCRules->getDisplayLabel(type), &exists);
	if (!exists)
		label.translate(AsciiString(typeName(type)));
	UnicodeString text;
	text.format(L"+%u ", amount);
	text.concat(label);
	return text;
}

// ------------------------------------------------------------------------------------------------
Bool AoCGather::logEnabled()
{
	return AoCHarness::isActive();
}

// ------------------------------------------------------------------------------------------------
void AoCGather::log( UnsignedInt objectId, const char *format, ... )
{
	if (!logEnabled())
		return;
	char text[512];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	text[sizeof(text) - 1] = '\0';
	fprintf(stderr, "[AoC gather] frame %u %u %s\n", TheGameLogic ? TheGameLogic->getFrame() : 0u, objectId, text);
	fflush(stderr);
}
