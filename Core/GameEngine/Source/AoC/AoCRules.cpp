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

// AoCRules.cpp
// Age of Commanders F1: the rules singleton and the AoCResourceType INI block. See AoCRules.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCRules.h"
#include "Common/INI.h"
#include "Common/Xfer.h"
#include "GameLogic/Scripts.h"

AoCRules *TheAoCRules = nullptr;

// ------------------------------------------------------------------------------------------------
AoCRules::AoCRules()
{
	m_typeCount = 0;
	m_definedByINI = FALSE;
	m_loadedTypeCount = 0;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		m_loadRemap[i] = -1;
	setImplicitCash();
}

// ------------------------------------------------------------------------------------------------
AoCRules::~AoCRules()
{
}

// ------------------------------------------------------------------------------------------------
void AoCRules::init()
{
	// INI definitions arrive after init() through loadDefinitions(); keep whatever is defined.
	m_loadedTypeCount = 0;
}

// ------------------------------------------------------------------------------------------------
void AoCRules::reset()
{
	// The type set is INI data and survives a game reset, like the sciences; only the load remap is per game.
	m_loadedTypeCount = 0;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
		m_loadRemap[i] = -1;
}

// ------------------------------------------------------------------------------------------------
void AoCRules::loadDefinitions( Xfer *pXferCRC )
{
	// Both locations are optional (stock data has neither), so the "expect a file" flag is dropped.
	INI ini;
	ini.loadFileDirectory("Data\\INI\\Default\\AoCResources", INI_LOAD_OVERWRITE, pXferCRC, INI::LoadFlags_SearchSubDirs);
	ini.loadFileDirectory("Data\\INI\\AoCResources", INI_LOAD_OVERWRITE, pXferCRC, INI::LoadFlags_SearchSubDirs);
}

// ------------------------------------------------------------------------------------------------
void AoCRules::setImplicitCash()
{
	m_types[AOC_RESOURCE_CASH].name = "Cash";
	m_types[AOC_RESOURCE_CASH].displayLabel.clear();
	m_types[AOC_RESOURCE_CASH].iconName.clear();
	m_types[AOC_RESOURCE_CASH].cashEquivalentPerMille = 1000;
	m_types[AOC_RESOURCE_CASH].order = 0;
	if (m_typeCount < 1)
		m_typeCount = 1;
}

// ------------------------------------------------------------------------------------------------
Int AoCRules::findType( const char *name ) const
{
	if (name == nullptr)
		return -1;
	for (Int i = 0; i < m_typeCount; ++i)
		if (m_types[i].name.compareNoCase(name) == 0)
			return i;
	return -1;
}

// ------------------------------------------------------------------------------------------------
const AsciiString &AoCRules::getTypeName( Int type ) const
{
	if (type < 0 || type >= m_typeCount)
		return AsciiString::TheEmptyString;
	return m_types[type].name;
}

// ------------------------------------------------------------------------------------------------
const AsciiString &AoCRules::getDisplayLabel( Int type ) const
{
	if (type < 0 || type >= m_typeCount)
		return AsciiString::TheEmptyString;
	return m_types[type].displayLabel;
}

// ------------------------------------------------------------------------------------------------
const AsciiString &AoCRules::getIconName( Int type ) const
{
	if (type < 0 || type >= m_typeCount)
		return AsciiString::TheEmptyString;
	return m_types[type].iconName;
}

// ------------------------------------------------------------------------------------------------
Int AoCRules::getCashEquivalentPerMille( Int type ) const
{
	if (type < 0 || type >= m_typeCount)
		return 0;
	return m_types[type].cashEquivalentPerMille;
}

// ------------------------------------------------------------------------------------------------
Int AoCRules::getOrder( Int type ) const
{
	if (type < 0 || type >= m_typeCount)
		return 0;
	return m_types[type].order;
}

// ------------------------------------------------------------------------------------------------
Int AoCRules::defineType( const AsciiString &name, const AsciiString &displayLabel, const AsciiString &iconName,
	Int cashEquivalentPerMille, Bool isCash, Int order )
{
	if (name.isEmpty())
		throw INI_INVALID_DATA;

	Int index = findType(name.str());
	if (isCash || index == AOC_RESOURCE_CASH)
	{
		if (index > AOC_RESOURCE_CASH)
		{
			DEBUG_CRASH(("AoCResourceType '%s' is already a non-cash type and cannot become cash", name.str()));
			throw INI_INVALID_DATA;
		}
		index = AOC_RESOURCE_CASH;
		cashEquivalentPerMille = 1000;	// cash is the unit
	}
	else if (index < 0)
	{
		if (m_typeCount >= AOC_MAX_RESOURCES)
		{
			DEBUG_CRASH(("AoCResourceType '%s': at most %d resource types are supported", name.str(), AOC_MAX_RESOURCES));
			throw INI_INVALID_DATA;
		}
		index = m_typeCount++;
	}

	if (cashEquivalentPerMille < 0)
		throw INI_INVALID_DATA;

	TypeInfo &info = m_types[index];
	info.name = name;
	info.displayLabel = displayLabel;
	info.iconName = iconName;
	info.cashEquivalentPerMille = cashEquivalentPerMille;
	info.order = (order >= 0) ? order : index;
	m_definedByINI = TRUE;
	return index;
}

// ------------------------------------------------------------------------------------------------
Int AoCRules::remapLoadedType( Int loadedType ) const
{
	if (m_loadedTypeCount <= 0)
		return (loadedType >= 0 && loadedType < m_typeCount) ? loadedType : -1;
	if (loadedType < 0 || loadedType >= m_loadedTypeCount || loadedType >= AOC_MAX_RESOURCES)
		return -1;
	return m_loadRemap[loadedType];
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCRules::parseResourceTypeDefinition( INI *ini )
{
	// The block is read into a scratch record and defined afterwards, so the whole block is consumed
	// even when no singleton exists (base Generals, tools).
	struct ScratchType
	{
		AsciiString displayLabel;
		AsciiString iconName;
		Int cashEquivalentPerMille;
		Bool isCash;
		Int order;
	};
	static const FieldParse fieldParse[] =
	{
		{ "DisplayName",    INI::parseAsciiString, nullptr, offsetof( ScratchType, displayLabel ) },
		{ "Icon",           INI::parseAsciiString, nullptr, offsetof( ScratchType, iconName ) },
		{ "CashEquivalent", INI::parseInt,         nullptr, offsetof( ScratchType, cashEquivalentPerMille ) },
		{ "IsCash",         INI::parseBool,        nullptr, offsetof( ScratchType, isCash ) },
		{ "Order",          INI::parseInt,         nullptr, offsetof( ScratchType, order ) },
		{ nullptr, nullptr, nullptr, 0 }
	};

	AsciiString name = ini->getNextToken();

	ScratchType scratch;
	scratch.cashEquivalentPerMille = 1000;
	scratch.isCash = FALSE;
	scratch.order = -1;
	ini->initFromINI(&scratch, fieldParse);

	if (TheAoCRules)
		TheAoCRules->defineType(name, scratch.displayLabel, scratch.iconName, scratch.cashEquivalentPerMille, scratch.isCash, scratch.order);
}

// ------------------------------------------------------------------------------------------------
/*static*/ Bool AoCRules::enabled()
{
	return TheAoCRules != nullptr && TheAoCRules->isEnabled();
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCRules::typeCount()
{
	return TheAoCRules ? TheAoCRules->getTypeCount() : 1;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCRules::findTypeIndex( const char *name )
{
	if (TheAoCRules)
		return TheAoCRules->findType(name);
	// Without a singleton only cash exists.
	return (name && stricmp(name, "Cash") == 0) ? AOC_RESOURCE_CASH : -1;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCRules::cashEquivalentPerMille( Int type )
{
	if (TheAoCRules)
		return TheAoCRules->getCashEquivalentPerMille(type);
	return (type == AOC_RESOURCE_CASH) ? 1000 : 0;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Bool AoCRules::compareInts( Int comparison, Int lhs, Int rhs )
{
	switch (comparison)
	{
		case Parameter::LESS_THAN:     return lhs <  rhs;
		case Parameter::LESS_EQUAL:    return lhs <= rhs;
		case Parameter::EQUAL:         return lhs == rhs;
		case Parameter::GREATER_EQUAL: return lhs >= rhs;
		case Parameter::GREATER:       return lhs >  rhs;
		case Parameter::NOT_EQUAL:     return lhs != rhs;
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
/** CRC: the type set is INI data shared by every machine and already part of the INI CRC. */
// ------------------------------------------------------------------------------------------------
void AoCRules::crc( Xfer * /*xfer*/ )
{
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version: the type names, so a save made with another type set loads by name.
	*/
// ------------------------------------------------------------------------------------------------
void AoCRules::xfer( Xfer *xfer )
{
	const XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	UnsignedByte count = (UnsignedByte)m_typeCount;
	xfer->xferUnsignedByte( &count );

	const Bool loading = (xfer->getXferMode() == XFER_LOAD);
	if (loading)
	{
		m_loadedTypeCount = count;
		for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
			m_loadRemap[i] = -1;
	}

	for (Int i = 0; i < (Int)count; ++i)
	{
		AsciiString name;
		if (!loading && i < m_typeCount)
			name = m_types[i].name;
		xfer->xferAsciiString( &name );
		if (loading && i < AOC_MAX_RESOURCES)
			m_loadRemap[i] = findType(name.str());
	}
}

// ------------------------------------------------------------------------------------------------
void AoCRules::loadPostProcess()
{
}
