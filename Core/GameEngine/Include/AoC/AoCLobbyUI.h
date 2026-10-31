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

// AoCLobbyUI.h
// Age of Commanders F7 (design D11 section 2.7, D6 section 7): the lobby option gadgets of the skirmish and LAN game
// options menus. Client code only; the menus call it from one hook line per stock site.
//
// The gadgets are stock combo boxes, a checkbox and static text labels that the mod's copies of
// SkirmishGameOptionsMenu.wnd and LanGameOptionsMenu.wnd add (names "<Layout>.wnd:ComboBoxAoCRichness",
// "...:ComboBoxAoCPopulationCap", "...:ComboBoxAoCVictory", "...:CheckboxAoCAmbientCivilians" and the labels
// "...:StaticTextAoCStartingStock", "...:StaticTextAoCRichness", "...:StaticTextAoCPopulationCap",
// "...:StaticTextAoCVictory"). Starting Stock replaces Starting Cash in place: the stock ComboBoxStartingCash lists the
// AoCStartingStockChoice entries when that list is defined. A gadget missing from the layout (the stock .wnd) is skipped;
// a row whose list is not defined is hidden, so the stock game and stock layouts are unchanged.

#pragma once

#include "Lib/BaseType.h"
#include "Common/NameKeyGenerator.h"

class GameInfo;
class GameWindow;

struct AoCLobbyGadgets
{
	GameWindow *stockLabel;
	GameWindow *stockCombo;				///< the stock ComboBoxStartingCash
	GameWindow *richnessLabel;
	GameWindow *richnessCombo;
	GameWindow *capLabel;
	GameWindow *capCombo;
	GameWindow *victoryLabel;
	GameWindow *victoryCombo;
	GameWindow *ambientCheck;
	NameKeyType stockComboID;
	NameKeyType richnessComboID;
	NameKeyType capComboID;
	NameKeyType victoryComboID;
	NameKeyType ambientCheckID;
	Bool stockShowsChoices;				///< populate() put the starting stock choices into the stock combo

	AoCLobbyGadgets() { clear(); }
	void clear();
};

class AoCLobbyUI
{
public:

	/// Look up the gadgets of one layout ("SkirmishGameOptionsMenu" or "LanGameOptionsMenu") under 'parent'. Clears 'g'
	/// first, so call it before anything can send the layout's selection messages.
	static void findGadgets( GameWindow *parent, const char *layout, AoCLobbyGadgets &g );

	/// TRUE when an AoCStartingStockChoice list is defined: the Starting Cash combo lists starting stocks instead, and the
	/// stock money list must not be put into it (PopulateStartingCashComboBox is skipped).
	static Bool replacesStartingCash();

	/// Fill every combo from its list (item data = the index), set the label texts (English fallbacks when the string
	/// table has no entry) and hide the rows whose list is not defined; then show the game's settings.
	static void populate( AoCLobbyGadgets &g, const GameInfo *game );

	/// Select the game's settings in the gadgets (after a change, a received options string, the initial preferences).
	static void show( AoCLobbyGadgets &g, const GameInfo *game );

	/// Host-only editing, as for the stock options: a LAN client sees the values but cannot change them.
	static void enable( AoCLobbyGadgets &g, Bool enabled );

	/**
		A GCM_SELECTED or GBM_SELECTED from 'controlID'. When it is one of the F7 gadgets (the stock combo only while it lists
		starting stocks), the game's lobby settings take the gadget's value and TRUE is returned; 'changed' tells whether
		a value actually changed (a selection the menu itself made sends the same message). FALSE for any other control.
	*/
	static Bool handleControl( AoCLobbyGadgets &g, Int controlID, GameInfo *game, Bool &changed );
};
