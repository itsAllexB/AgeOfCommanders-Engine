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

// AoCLobbyRules.h
// Age of Commanders F7 (design D11 section 2.7, D1 section 6, D2 section 7, D6 section 7): the lobby options.
//
// Five fields travel with a game: starting stock, resource richness, population cap (indices into INI choice
// lists), ambient civilians (on or off) and victory mode (an index into AoCVictoryMode). The choice lists are
// global INI data, registered and stored like the stock MultiplayerStartingMoneyChoice (overrides rejected):
//
//   AoCStartingStockChoice
//     Name    = GUI:AoCStockStandard                  ; label (also the remembered preference)
//     Value   = Food:200 Wood:200 Steel:0 Cash:500    ; an AoCCost: the deposit at game start, Cash is the starting cash
//     Default = Yes
//   End
//   AoCRichnessChoice      Name = GUI:AoCRichnessPoor  ScalePerMille = 600   End   ; flagged warehouses x 0.6
//   AoCPopulationCapChoice Name = GUI:AoCPopStandard   Cap = 150             End   ; 0 no lobby clamp, -1 unlimited
//
// Loaded from Data\INI\Default\AoCLobby and Data\INI\AoCLobby (files or folders, both optional) right after the
// resource types, so a Value can name them; a block in any other global INI file (Multiplayer.ini) works too.
// The INI CRC covers them, so every peer has the same lists and the options string carries plain indices.
//
// The data is the flag (D11 1.6): with no list defined every field is at its default (sanitize forces ambient on and
// conquest too, so a hand-made string or a remembered preference cannot change a stock game), the options string
// carries no AoC key, starting stock and population cap keep their GameData.ini sources (F1 AoCStartingStock, F4
// AoCDefaultPopulationCap) and richness is 1000 per mille, so the stock game is unchanged.
//
// Options string (decision 1, one combined key since the review of PR #6, finding 2): AO=<s><r><p><a><v>; with one
// hex digit each for the starting stock, richness and population cap indices (0 to F: a list holds at most 16
// choices), Y or N for ambient civilians and one digit for the victory mode, e.g. "AO=220N0;". Written only when at
// least one field differs from its default, after the stock header keys and before the slot list; a missing key means
// every default. A value that is not exactly five characters of that shape rejects the string (malformed); an index
// outside its list becomes the list's default when the game stores it. A stock parser rejects unknown keys, so a
// default game keeps a stock-readable string and replay header. Nine characters at most of the LAN options budget.
//
// During play the logic reads the population cap, ambient civilians, victory and the richness factor from GameLogic
// (copied in startNewGame, saved by GameLogic::xfer, in the per-frame CRC through PlayerList::crc when not the
// default), never from TheGameInfo. Starting stock is applied in Player::init and richness at the end of
// GameLogic::startNewGame, both at the start of a new game only.

#pragma once

#include "AoC/AoCCost.h"
#include "Common/AsciiString.h"
#include "Common/SubsystemInterface.h"

#include <vector>

class INI;
class UserPreferences;
class Xfer;

/// Victory modes (D1 section 6). Only conquest exists in v1; wonder and score are stored and do nothing yet.
enum AoCVictoryMode CPP_11(: Int)
{
	AOC_VICTORY_CONQUEST = 0,
	AOC_VICTORY_WONDER,
	AOC_VICTORY_SCORE,

	AOC_VICTORY_COUNT
};

enum
{
	AOC_LOBBY_MAX_CHOICES = 16,		///< per list: two digits at most in the options string (LAN length budget)
	AOC_RICHNESS_DEFAULT_PER_MILLE = 1000,
};

/// The five lobby fields of one game, as indices (decision 1).
struct AoCLobbySettings
{
	Int startingStock;		///< index into the AoCStartingStockChoice list
	Int richness;					///< index into the AoCRichnessChoice list
	Int populationCap;		///< index into the AoCPopulationCapChoice list
	Bool ambientCivilians;
	Int victory;					///< AoCVictoryMode

	AoCLobbySettings() : startingStock(0), richness(0), populationCap(0), ambientCivilians(TRUE), victory(AOC_VICTORY_CONQUEST) { }

	Bool operator==( const AoCLobbySettings &o ) const
	{
		return startingStock == o.startingStock && richness == o.richness && populationCap == o.populationCap
			&& ambientCivilians == o.ambientCivilians && victory == o.victory;
	}
	Bool operator!=( const AoCLobbySettings &o ) const { return !(*this == o); }
};

struct AoCStartingStockChoice
{
	AsciiString name;
	AoCCost value;
};

struct AoCRichnessChoice
{
	AsciiString name;
	Int scalePerMille;
};

struct AoCPopulationCapChoice
{
	AsciiString name;
	Int cap;		///< 0 = no lobby clamp (housing limits), -1 = unlimited (the check is off), else the cap
};

class AoCLobbyRules : public SubsystemInterface
{
public:

	AoCLobbyRules();
	virtual ~AoCLobbyRules() override;

	virtual void init() override { }
	virtual void reset() override { }		///< the lists are INI data and survive a game reset, like the money choices
	virtual void update() override { }

	/// Load Data\INI\Default\AoCLobby(.ini and folder) and Data\INI\AoCLobby; both optional.
	void loadDefinitions( Xfer *pXferCRC );

	// The lists. A getter returns nullptr for an index outside its list.
	Int getStartingStockCount() const { return (Int)m_stock.size(); }
	Int getRichnessCount() const { return (Int)m_richness.size(); }
	Int getPopulationCapCount() const { return (Int)m_caps.size(); }
	const AoCStartingStockChoice *getStartingStock( Int index ) const;
	const AoCRichnessChoice *getRichness( Int index ) const;
	const AoCPopulationCapChoice *getPopulationCap( Int index ) const;
	/// The choice that says Default = Yes, else the first one; 0 for an empty list.
	Int getStartingStockDefault() const { return m_stockDefault; }
	Int getRichnessDefault() const { return m_richnessDefault; }
	Int getPopulationCapDefault() const { return m_capDefault; }

	/**
		Append a choice (INI blocks and unit tests); returns its index. A second Default = Yes in one list, more than
		AOC_LOBBY_MAX_CHOICES choices, an empty name, a richness below 1 per mille or a cap below -1 throws
		INI_INVALID_DATA (the stock list asserts on a second default; a lobby list must be unambiguous).
	*/
	Int addStartingStockChoice( const AsciiString &name, const AoCCost &value, Bool isDefault );
	Int addRichnessChoice( const AsciiString &name, Int scalePerMille, Bool isDefault );
	Int addPopulationCapChoice( const AsciiString &name, Int cap, Bool isDefault );
	void clear();

	/// INI block parsers (registered in INI.cpp's block table). A block read with INI_LOAD_CREATE_OVERRIDES (a map's
	/// map.ini or solo.ini) is consumed and ignored on every machine: the lists are global, as for the money choices.
	static void parseStartingStockChoiceDefinition( INI *ini );
	static void parseRichnessChoiceDefinition( INI *ini );
	static void parsePopulationCapChoiceDefinition( INI *ini );

	// --- Everything below tolerates a missing singleton (unit tests, tools, the base Generals game). ---

	/// TRUE when any choice list is defined: the lobby fields mean something (harness lines, UI rows).
	static Bool anyListDefined();
	static Bool hasStartingStockList();
	static Bool hasRichnessList();
	static Bool hasPopulationCapList();

	/// Default of every field: each list's default index, ambient civilians on, conquest.
	static AoCLobbySettings defaults();
	/// Every index brought into its list (an index outside a defined list becomes that list's default; with no list the
	/// index is the default), victory into AoCVictoryMode. With no list at all, ambient civilians on and conquest too
	/// (review of PR #6, finding 1): a stock game always runs at the defaults.
	static AoCLobbySettings sanitize( const AoCLobbySettings &s );

	// Options string (decision 1; the combined key of the review of PR #6, finding 2).
	/// Append "AO=<s><r><p><a><v>;" when any field differs from its default; nothing otherwise.
	static void appendOptionsString( const AoCLobbySettings &s, AsciiString &out );
	/// One key of ParseAsciiStringToGameInfo: TRUE when 'key' is AO (the five fields then land in 's'); a value that is
	/// not exactly three hex digits, Y or N, and a hex digit sets 'valueOk' FALSE and leaves 's' alone.
	static Bool parseOptionsKey( const AsciiString &key, const AsciiString &val, AoCLobbySettings &s, Bool &valueOk );

	// Index to value.
	/// The game's population cap: the chosen Cap when a cap list is defined, else 'fallbackCap' (F4's
	/// GameData.ini AoCDefaultPopulationCap).
	static Int populationCapFor( const AoCLobbySettings &s, Int fallbackCap );
	/// The chosen ScalePerMille, AOC_RICHNESS_DEFAULT_PER_MILLE without a list.
	static Int richnessPerMilleFor( const AoCLobbySettings &s );
	/// What Player::init deposits besides the starting cash. With a stock list: the chosen vector without its cash
	/// (the cash is the game's starting cash, set from the same choice, decision 4). Without a list: 'fallback' whole
	/// (F1's GameData.ini AoCStartingStock, whose cash component adds to the lobby cash).
	static AoCCost startingStockDepositFor( const AoCLobbySettings &s, const AoCCost &fallback );
	/// The starting cash a stock choice implies: TRUE and 'cash' with a stock list, FALSE without one.
	static Bool startingCashFor( const AoCLobbySettings &s, UnsignedInt &cash );
	/// Richness arithmetic (decision 5): boxes * perMille / 1000, rounded down, never below 1 for a non-empty node;
	/// an empty node stays empty. Integer arithmetic.
	static Int scaleBoxes( Int boxes, Int perMille );

	// Names: the choice's Name, or the index as a number when the list has no such choice.
	static AsciiString startingStockName( Int index );
	static AsciiString richnessName( Int index );
	static AsciiString populationCapName( Int index );
	static const char *victoryName( Int victory );			///< "Conquest", "Wonder", "Score"
	static const char *victoryLabel( Int victory );		///< "GUI:AoCVictoryConquest" and so on

	// Remembered choices (preference keys beside StartingCash), stored by name so a reordered list keeps them.
	/// Fields with a remembered, still defined choice are set in 's'; the others are left as they are. Ambient civilians
	/// and victory are read only while some list is defined, as writePreferences writes them (review of PR #6, finding 1:
	/// the preference files are shared between mod and stock sessions).
	static void readPreferences( const UserPreferences &prefs, AoCLobbySettings &s );
	static void writePreferences( UserPreferences &prefs, const AoCLobbySettings &s );

private:

	std::vector<AoCStartingStockChoice> m_stock;
	std::vector<AoCRichnessChoice> m_richness;
	std::vector<AoCPopulationCapChoice> m_caps;
	Int m_stockDefault;
	Int m_richnessDefault;
	Int m_capDefault;
	Bool m_stockDefaultSet;
	Bool m_richnessDefaultSet;
	Bool m_capDefaultSet;
};

extern AoCLobbyRules *TheAoCLobbyRules;
