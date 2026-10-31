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

// AoCRules.h
// Age of Commanders F1 (design D11 sections 1.6 and 2.1): the logic-side rules singleton. It holds the
// resource types read from the AoCResourceType INI blocks:
//
//   AoCResourceType Wood
//     DisplayName    = GUI:AoCWood   ; label (F6 translates it)
//     Icon           = AoCWoodIcon   ; mapped image name (F6)
//     CashEquivalent = 1000          ; per mille of one cash unit (D2: food 1000, wood 1000, iron 1500, steel 4000)
//     IsCash         = No            ; Yes on at most one block, which then names index 0
//     Order          = 2             ; tray order (F6); defaults to the definition order
//   End
//
// The flag is the data: with no block defined isEnabled() is FALSE, only cash exists and every hook
// stays on the stock path. Index 0 is always cash; if no block says IsCash the implicit type "Cash" is
// used. The object is a Snapshot so a save game records the type names; a save made with a different
// type set loads by name (remapLoadedType). TheAoCRules is created by GameEngine::init before
// GameData.ini is parsed, because AoCStartingStock and every AoCCost resolve names at parse time.

#pragma once

#include "AoC/AoCCost.h"
#include "Common/AsciiString.h"
#include "Common/Snapshot.h"
#include "Common/SubsystemInterface.h"

class INI;
class Xfer;

class AoCRules : public SubsystemInterface, public Snapshot
{
public:

	AoCRules();
	virtual ~AoCRules() override;

	virtual void init() override;
	virtual void reset() override;
	virtual void update() override { }

	/// Load Data\INI\Default\AoCResources(.ini and folder) and Data\INI\AoCResources; both optional.
	void loadDefinitions( Xfer *pXferCRC );

	/// TRUE once at least one AoCResourceType block was defined (the feature flag, D11 1.6).
	Bool isEnabled() const { return m_definedByINI; }
	/// Number of types including cash at index 0; 1 when the feature is off.
	Int getTypeCount() const { return m_typeCount; }
	/// Index of a type by name (case-insensitive), -1 if unknown.
	Int findType( const char *name ) const;
	const AsciiString &getTypeName( Int type ) const;
	const AsciiString &getDisplayLabel( Int type ) const;
	const AsciiString &getIconName( Int type ) const;
	Int getCashEquivalentPerMille( Int type ) const;
	Int getOrder( Int type ) const;

	/**
		Define or redefine a type (INI blocks and unit tests). isCash names index 0 (its CashEquivalent
		is always 1000). A new non-cash name is appended; an existing name is updated in place.
		Returns the index, or throws INI_INVALID_DATA when AOC_MAX_RESOURCES is exceeded.
	*/
	Int defineType( const AsciiString &name, const AsciiString &displayLabel, const AsciiString &iconName,
		Int cashEquivalentPerMille, Bool isCash, Int order );

	/// Save-game support: the index a type had in the loaded file mapped to its current index, -1 if gone.
	Int remapLoadedType( Int loadedType ) const;

	/// INI block parser for "AoCResourceType <Name>" (registered in INI.cpp's block table). A block read
	/// with INI_LOAD_CREATE_OVERRIDES (a map's map.ini or solo.ini) is consumed and ignored: the type set
	/// is global and outlives the game.
	static void parseResourceTypeDefinition( INI *ini );

	// Helpers that tolerate a missing singleton (unit tests, tools, the base Generals game).
	static Bool enabled();
	static Int typeCount();
	static Int findTypeIndex( const char *name );
	static Int cashEquivalentPerMille( Int type );
	/// Script comparison (Parameter::LESS_THAN and friends): lhs <comparison> rhs.
	static Bool compareInts( Int comparison, Int lhs, Int rhs );

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;

private:

	struct TypeInfo
	{
		AsciiString name;
		AsciiString displayLabel;
		AsciiString iconName;
		Int cashEquivalentPerMille;
		Int order;
	};

	void setImplicitCash();

	TypeInfo m_types[AOC_MAX_RESOURCES];
	Int m_typeCount;
	Bool m_definedByINI;

	// set while a save game loads: how the file's type indices map onto the current set
	Int m_loadedTypeCount;
	Int m_loadRemap[AOC_MAX_RESOURCES];
};

extern AoCRules *TheAoCRules;
