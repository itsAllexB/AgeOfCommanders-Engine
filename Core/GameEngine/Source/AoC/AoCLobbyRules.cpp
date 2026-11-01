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

// Age of Commanders F7: the lobby choice lists, the options string keys and the index-to-value mapping.
// See AoCLobbyRules.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCLobbyRules.h"
#include "Common/INI.h"
#include "Common/UserPreferences.h"

#include <stdlib.h>

AoCLobbyRules *TheAoCLobbyRules = nullptr;

namespace
{

const char AOC_PREF_STARTING_STOCK[] = "AoCStartingStock";
const char AOC_PREF_RICHNESS[] = "AoCRichness";
const char AOC_PREF_POPULATION_CAP[] = "AoCPopulationCap";
const char AOC_PREF_AMBIENT[] = "AoCAmbientCivilians";
const char AOC_PREF_VICTORY[] = "AoCVictory";

const char *const s_victoryNames[AOC_VICTORY_COUNT] = { "Conquest", "Wonder", "Score" };
const char *const s_victoryLabels[AOC_VICTORY_COUNT] = { "GUI:AoCVictoryConquest", "GUI:AoCVictoryWonder", "GUI:AoCVictoryScore" };

const char AOC_OPTIONS_KEY[] = "AO";
const Int AOC_OPTIONS_VALUE_LENGTH = 5;	// <stock><richness><cap><ambient><victory>

// One hex digit (either case) to 0..15, or -1.
Int hexDigit( char c )
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

// 0..15 as one upper-case hex digit; an index outside that range cannot come from a list (AOC_LOBBY_MAX_CHOICES) and
// is written as its default's digit by the caller, so it never reaches here.
char hexChar( Int value )
{
	static const char digits[] = "0123456789ABCDEF";
	return digits[value & 0xF];
}

Int clampIndex( Int index, Int count, Int defaultIndex )
{
	if (count <= 0)
		return 0;
	if (index < 0 || index >= count)
		return defaultIndex;
	return index;
}

// The block's fields, read into scratch storage so the whole block is consumed even without a singleton.
struct StockScratch { AsciiString name; AoCCost value; Bool isDefault; };
struct RichnessScratch { AsciiString name; Int scalePerMille; Bool isDefault; };
struct CapScratch { AsciiString name; Int cap; Bool isDefault; };

// Map blocks are rejected as stock rejects MultiplayerStartingMoneyChoice overrides, and also ignored in release builds:
// a map must not change a global list that the next lobby indexes.
Bool rejectOverride( INI *ini, const char *block )
{
	if (ini->getLoadType() != INI_LOAD_CREATE_OVERRIDES)
		return FALSE;
	DEBUG_CRASH(("%s in '%s' ignored: overrides are not supported for the lobby choice lists", block, ini->getFilename().str()));
	return TRUE;
}

} // namespace

// ------------------------------------------------------------------------------------------------
AoCLobbyRules::AoCLobbyRules()
{
	clear();
}

// ------------------------------------------------------------------------------------------------
AoCLobbyRules::~AoCLobbyRules()
{
}

// ------------------------------------------------------------------------------------------------
void AoCLobbyRules::clear()
{
	m_stock.clear();
	m_richness.clear();
	m_caps.clear();
	m_stockDefault = 0;
	m_richnessDefault = 0;
	m_capDefault = 0;
	m_stockDefaultSet = FALSE;
	m_richnessDefaultSet = FALSE;
	m_capDefaultSet = FALSE;
}

// ------------------------------------------------------------------------------------------------
void AoCLobbyRules::loadDefinitions( Xfer *pXferCRC )
{
	// Both locations are optional (stock data has neither), so the "expect a file" flag is dropped.
	INI ini;
	ini.loadFileDirectory("Data\\INI\\Default\\AoCLobby", INI_LOAD_OVERWRITE, pXferCRC, INI::LoadFlags_SearchSubDirs);
	ini.loadFileDirectory("Data\\INI\\AoCLobby", INI_LOAD_OVERWRITE, pXferCRC, INI::LoadFlags_SearchSubDirs);
}

// ------------------------------------------------------------------------------------------------
const AoCStartingStockChoice *AoCLobbyRules::getStartingStock( Int index ) const
{
	return (index >= 0 && index < (Int)m_stock.size()) ? &m_stock[index] : nullptr;
}

// ------------------------------------------------------------------------------------------------
const AoCRichnessChoice *AoCLobbyRules::getRichness( Int index ) const
{
	return (index >= 0 && index < (Int)m_richness.size()) ? &m_richness[index] : nullptr;
}

// ------------------------------------------------------------------------------------------------
const AoCPopulationCapChoice *AoCLobbyRules::getPopulationCap( Int index ) const
{
	return (index >= 0 && index < (Int)m_caps.size()) ? &m_caps[index] : nullptr;
}

// ------------------------------------------------------------------------------------------------
Int AoCLobbyRules::addStartingStockChoice( const AsciiString &name, const AoCCost &value, Bool isDefault )
{
	if (name.isEmpty() || (Int)m_stock.size() >= AOC_LOBBY_MAX_CHOICES || (isDefault && m_stockDefaultSet))
	{
		DEBUG_CRASH(("AoCStartingStockChoice '%s': a name is required, at most %d choices, and only one Default", name.str(), AOC_LOBBY_MAX_CHOICES));
		throw INI_INVALID_DATA;
	}
	AoCStartingStockChoice choice;
	choice.name = name;
	choice.value = value;
	m_stock.push_back(choice);
	const Int index = (Int)m_stock.size() - 1;
	if (isDefault)
	{
		m_stockDefault = index;
		m_stockDefaultSet = TRUE;
	}
	return index;
}

// ------------------------------------------------------------------------------------------------
Int AoCLobbyRules::addRichnessChoice( const AsciiString &name, Int scalePerMille, Bool isDefault )
{
	if (name.isEmpty() || scalePerMille < 1 || (Int)m_richness.size() >= AOC_LOBBY_MAX_CHOICES || (isDefault && m_richnessDefaultSet))
	{
		DEBUG_CRASH(("AoCRichnessChoice '%s': a name, ScalePerMille of 1 or more, at most %d choices and only one Default", name.str(), AOC_LOBBY_MAX_CHOICES));
		throw INI_INVALID_DATA;
	}
	AoCRichnessChoice choice;
	choice.name = name;
	choice.scalePerMille = scalePerMille;
	m_richness.push_back(choice);
	const Int index = (Int)m_richness.size() - 1;
	if (isDefault)
	{
		m_richnessDefault = index;
		m_richnessDefaultSet = TRUE;
	}
	return index;
}

// ------------------------------------------------------------------------------------------------
Int AoCLobbyRules::addPopulationCapChoice( const AsciiString &name, Int cap, Bool isDefault )
{
	if (name.isEmpty() || cap < -1 || (Int)m_caps.size() >= AOC_LOBBY_MAX_CHOICES || (isDefault && m_capDefaultSet))
	{
		DEBUG_CRASH(("AoCPopulationCapChoice '%s': a name, Cap of -1 or more, at most %d choices and only one Default", name.str(), AOC_LOBBY_MAX_CHOICES));
		throw INI_INVALID_DATA;
	}
	AoCPopulationCapChoice choice;
	choice.name = name;
	choice.cap = cap;
	m_caps.push_back(choice);
	const Int index = (Int)m_caps.size() - 1;
	if (isDefault)
	{
		m_capDefault = index;
		m_capDefaultSet = TRUE;
	}
	return index;
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCLobbyRules::parseStartingStockChoiceDefinition( INI *ini )
{
	static const FieldParse fieldParse[] =
	{
		{ "Name",    INI::parseAsciiString,           nullptr, offsetof( StockScratch, name ) },
		{ "Value",   AoCCost::parseAoCCostField,      nullptr, offsetof( StockScratch, value ) },
		{ "Default", INI::parseBool,                  nullptr, offsetof( StockScratch, isDefault ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	StockScratch scratch;
	scratch.isDefault = FALSE;
	ini->initFromINI(&scratch, fieldParse);
	if (rejectOverride(ini, "AoCStartingStockChoice"))
		return;
	if (TheAoCLobbyRules)
		TheAoCLobbyRules->addStartingStockChoice(scratch.name, scratch.value, scratch.isDefault);
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCLobbyRules::parseRichnessChoiceDefinition( INI *ini )
{
	static const FieldParse fieldParse[] =
	{
		{ "Name",          INI::parseAsciiString, nullptr, offsetof( RichnessScratch, name ) },
		{ "ScalePerMille", INI::parseInt,         nullptr, offsetof( RichnessScratch, scalePerMille ) },
		{ "Default",       INI::parseBool,        nullptr, offsetof( RichnessScratch, isDefault ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	RichnessScratch scratch;
	scratch.scalePerMille = AOC_RICHNESS_DEFAULT_PER_MILLE;
	scratch.isDefault = FALSE;
	ini->initFromINI(&scratch, fieldParse);
	if (rejectOverride(ini, "AoCRichnessChoice"))
		return;
	if (TheAoCLobbyRules)
		TheAoCLobbyRules->addRichnessChoice(scratch.name, scratch.scalePerMille, scratch.isDefault);
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCLobbyRules::parsePopulationCapChoiceDefinition( INI *ini )
{
	static const FieldParse fieldParse[] =
	{
		{ "Name",    INI::parseAsciiString, nullptr, offsetof( CapScratch, name ) },
		{ "Cap",     INI::parseInt,         nullptr, offsetof( CapScratch, cap ) },
		{ "Default", INI::parseBool,        nullptr, offsetof( CapScratch, isDefault ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	CapScratch scratch;
	scratch.cap = 0;
	scratch.isDefault = FALSE;
	ini->initFromINI(&scratch, fieldParse);
	if (rejectOverride(ini, "AoCPopulationCapChoice"))
		return;
	if (TheAoCLobbyRules)
		TheAoCLobbyRules->addPopulationCapChoice(scratch.name, scratch.cap, scratch.isDefault);
}

// ------------------------------------------------------------------------------------------------
/*static*/ Bool AoCLobbyRules::hasStartingStockList()
{
	return TheAoCLobbyRules != nullptr && TheAoCLobbyRules->getStartingStockCount() > 0;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Bool AoCLobbyRules::hasRichnessList()
{
	return TheAoCLobbyRules != nullptr && TheAoCLobbyRules->getRichnessCount() > 0;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Bool AoCLobbyRules::hasPopulationCapList()
{
	return TheAoCLobbyRules != nullptr && TheAoCLobbyRules->getPopulationCapCount() > 0;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Bool AoCLobbyRules::anyListDefined()
{
	return hasStartingStockList() || hasRichnessList() || hasPopulationCapList();
}

// ------------------------------------------------------------------------------------------------
/*static*/ AoCLobbySettings AoCLobbyRules::defaults()
{
	AoCLobbySettings s;
	if (TheAoCLobbyRules)
	{
		s.startingStock = TheAoCLobbyRules->getStartingStockDefault();
		s.richness = TheAoCLobbyRules->getRichnessDefault();
		s.populationCap = TheAoCLobbyRules->getPopulationCapDefault();
	}
	s.ambientCivilians = TRUE;
	s.victory = AOC_VICTORY_CONQUEST;
	return s;
}

// ------------------------------------------------------------------------------------------------
/*static*/ AoCLobbySettings AoCLobbyRules::sanitize( const AoCLobbySettings &in )
{
	AoCLobbySettings s = in;
	const AoCLobbySettings d = defaults();
	if (TheAoCLobbyRules)
	{
		s.startingStock = clampIndex(s.startingStock, TheAoCLobbyRules->getStartingStockCount(), d.startingStock);
		s.richness = clampIndex(s.richness, TheAoCLobbyRules->getRichnessCount(), d.richness);
		s.populationCap = clampIndex(s.populationCap, TheAoCLobbyRules->getPopulationCapCount(), d.populationCap);
	}
	else
	{
		s.startingStock = d.startingStock;
		s.richness = d.richness;
		s.populationCap = d.populationCap;
	}
	if (s.victory < 0 || s.victory >= AOC_VICTORY_COUNT)
		s.victory = d.victory;
	s.ambientCivilians = s.ambientCivilians ? TRUE : FALSE;
	if (!anyListDefined())
	{
		// Review of PR #6, finding 1: with no list at all the game is a stock game, so ambient civilians and victory
		// keep their defaults too, whatever a hand-made string or a preference file says.
		s.ambientCivilians = d.ambientCivilians;
		s.victory = d.victory;
	}
	return s;
}

// ------------------------------------------------------------------------------------------------
/** The combined key (review of PR #6, finding 2): "AO=" and five characters, the three list indices as one hex digit
	* each, Y or N, the victory digit; written only when some field differs from its default. */
// ------------------------------------------------------------------------------------------------
/*static*/ void AoCLobbyRules::appendOptionsString( const AoCLobbySettings &s, AsciiString &out )
{
	const AoCLobbySettings d = defaults();
	if (s == d)
		return;
	const Int indices[3] = { s.startingStock, s.richness, s.populationCap };
	const Int fallback[3] = { d.startingStock, d.richness, d.populationCap };
	char value[AOC_OPTIONS_VALUE_LENGTH + 1];
	for (Int i = 0; i < 3; ++i)
		value[i] = hexChar((indices[i] >= 0 && indices[i] < AOC_LOBBY_MAX_CHOICES) ? indices[i] : fallback[i]);
	value[3] = s.ambientCivilians ? 'Y' : 'N';
	value[4] = hexChar((s.victory >= 0 && s.victory < AOC_VICTORY_COUNT) ? s.victory : d.victory);
	value[5] = '\0';
	AsciiString part;
	part.format("%s=%s;", AOC_OPTIONS_KEY, value);
	out.concat(part);
}

// ------------------------------------------------------------------------------------------------
/*static*/ Bool AoCLobbyRules::parseOptionsKey( const AsciiString &key, const AsciiString &val, AoCLobbySettings &s, Bool &valueOk )
{
	if (key.compare(AOC_OPTIONS_KEY) != 0)
		return FALSE;

	const char *v = val.str();
	if (val.getLength() != AOC_OPTIONS_VALUE_LENGTH)
	{
		valueOk = FALSE;
		return TRUE;
	}
	const Int stock = hexDigit(v[0]);
	const Int richness = hexDigit(v[1]);
	const Int cap = hexDigit(v[2]);
	const Int victory = hexDigit(v[4]);
	const char ambient = v[3];
	if (stock < 0 || richness < 0 || cap < 0 || victory < 0 || (ambient != 'Y' && ambient != 'N' && ambient != 'y' && ambient != 'n'))
	{
		valueOk = FALSE;
		return TRUE;
	}
	s.startingStock = stock;
	s.richness = richness;
	s.populationCap = cap;
	s.ambientCivilians = (ambient == 'Y' || ambient == 'y') ? TRUE : FALSE;
	s.victory = victory;
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCLobbyRules::populationCapFor( const AoCLobbySettings &s, Int fallbackCap )
{
	if (!hasPopulationCapList())
		return fallbackCap;
	const AoCPopulationCapChoice *c = TheAoCLobbyRules->getPopulationCap(sanitize(s).populationCap);
	return c ? c->cap : fallbackCap;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCLobbyRules::richnessPerMilleFor( const AoCLobbySettings &s )
{
	if (!hasRichnessList())
		return AOC_RICHNESS_DEFAULT_PER_MILLE;
	const AoCRichnessChoice *c = TheAoCLobbyRules->getRichness(sanitize(s).richness);
	return c ? c->scalePerMille : AOC_RICHNESS_DEFAULT_PER_MILLE;
}

// ------------------------------------------------------------------------------------------------
/*static*/ AoCCost AoCLobbyRules::startingStockDepositFor( const AoCLobbySettings &s, const AoCCost &fallback )
{
	if (!hasStartingStockList())
		return fallback;
	AoCCost deposit;
	const AoCStartingStockChoice *c = TheAoCLobbyRules->getStartingStock(sanitize(s).startingStock);
	if (c)
	{
		deposit = c->value;
		deposit.amount[AOC_RESOURCE_CASH] = 0;	// the cash arrives as the game's starting cash (Player::init, stock code)
	}
	return deposit;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Bool AoCLobbyRules::startingCashFor( const AoCLobbySettings &s, UnsignedInt &cash )
{
	if (!hasStartingStockList())
		return FALSE;
	const AoCStartingStockChoice *c = TheAoCLobbyRules->getStartingStock(sanitize(s).startingStock);
	if (c == nullptr)
		return FALSE;
	cash = c->value.amount[AOC_RESOURCE_CASH];
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCLobbyRules::scaleBoxes( Int boxes, Int perMille )
{
	if (boxes <= 0)
		return boxes;
	if (perMille < 0)
		perMille = 0;
	Int64 scaled = ((Int64)boxes * (Int64)perMille) / 1000;
	if (scaled < 1)
		scaled = 1;
	if (scaled > 0x7FFFFFFF)
		scaled = 0x7FFFFFFF;
	return (Int)scaled;
}

// ------------------------------------------------------------------------------------------------
/*static*/ AsciiString AoCLobbyRules::startingStockName( Int index )
{
	const AoCStartingStockChoice *c = TheAoCLobbyRules ? TheAoCLobbyRules->getStartingStock(index) : nullptr;
	if (c)
		return c->name;
	AsciiString s;
	s.format("%d", index);
	return s;
}

// ------------------------------------------------------------------------------------------------
/*static*/ AsciiString AoCLobbyRules::richnessName( Int index )
{
	const AoCRichnessChoice *c = TheAoCLobbyRules ? TheAoCLobbyRules->getRichness(index) : nullptr;
	if (c)
		return c->name;
	AsciiString s;
	s.format("%d", index);
	return s;
}

// ------------------------------------------------------------------------------------------------
/*static*/ AsciiString AoCLobbyRules::populationCapName( Int index )
{
	const AoCPopulationCapChoice *c = TheAoCLobbyRules ? TheAoCLobbyRules->getPopulationCap(index) : nullptr;
	if (c)
		return c->name;
	AsciiString s;
	s.format("%d", index);
	return s;
}

// ------------------------------------------------------------------------------------------------
/*static*/ const char *AoCLobbyRules::victoryName( Int victory )
{
	return (victory >= 0 && victory < AOC_VICTORY_COUNT) ? s_victoryNames[victory] : "?";
}

// ------------------------------------------------------------------------------------------------
/*static*/ const char *AoCLobbyRules::victoryLabel( Int victory )
{
	return (victory >= 0 && victory < AOC_VICTORY_COUNT) ? s_victoryLabels[victory] : "";
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCLobbyRules::readPreferences( const UserPreferences &prefs, AoCLobbySettings &s )
{
	if (TheAoCLobbyRules)
	{
		UserPreferences::const_iterator it = prefs.find(AOC_PREF_STARTING_STOCK);
		if (it != prefs.end())
			for (Int i = 0; i < TheAoCLobbyRules->getStartingStockCount(); ++i)
				if (TheAoCLobbyRules->getStartingStock(i)->name.compareNoCase(it->second) == 0)
					s.startingStock = i;
		it = prefs.find(AOC_PREF_RICHNESS);
		if (it != prefs.end())
			for (Int i = 0; i < TheAoCLobbyRules->getRichnessCount(); ++i)
				if (TheAoCLobbyRules->getRichness(i)->name.compareNoCase(it->second) == 0)
					s.richness = i;
		it = prefs.find(AOC_PREF_POPULATION_CAP);
		if (it != prefs.end())
			for (Int i = 0; i < TheAoCLobbyRules->getPopulationCapCount(); ++i)
				if (TheAoCLobbyRules->getPopulationCap(i)->name.compareNoCase(it->second) == 0)
					s.populationCap = i;
	}
	// Review of PR #6, finding 1: the preference files are shared between mod and stock sessions, so ambient civilians
	// and victory are read only while some list is defined, as writePreferences writes them.
	if (!anyListDefined())
		return;
	UserPreferences::const_iterator it = prefs.find(AOC_PREF_AMBIENT);
	if (it != prefs.end())
	{
		s.ambientCivilians = (it->second.compareNoCase("no") != 0);
	}
	it = prefs.find(AOC_PREF_VICTORY);
	if (it != prefs.end())
	{
		for (Int v = 0; v < AOC_VICTORY_COUNT; ++v)
		{
			if (it->second.compareNoCase(s_victoryNames[v]) == 0)
				s.victory = v;
		}
	}
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCLobbyRules::writePreferences( UserPreferences &prefs, const AoCLobbySettings &s )
{
	// Only the lists that exist are remembered, so a stock install writes nothing new.
	if (hasStartingStockList())
		prefs[AOC_PREF_STARTING_STOCK] = startingStockName(s.startingStock);
	if (hasRichnessList())
		prefs[AOC_PREF_RICHNESS] = richnessName(s.richness);
	if (hasPopulationCapList())
		prefs[AOC_PREF_POPULATION_CAP] = populationCapName(s.populationCap);
	if (anyListDefined())
	{
		prefs[AOC_PREF_AMBIENT] = s.ambientCivilians ? "Yes" : "No";
		prefs[AOC_PREF_VICTORY] = victoryName(s.victory);
	}
}
