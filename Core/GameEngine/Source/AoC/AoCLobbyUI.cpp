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

// Age of Commanders F7: the lobby option gadgets of the skirmish and LAN game options menus. See AoCLobbyUI.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCLobbyUI.h"
#include "AoC/AoCLobbyRules.h"

#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameNetwork/GameInfo.h"

#include <stdint.h>

namespace
{

GameWindow *findChild( GameWindow *parent, const char *layout, const char *name, NameKeyType *idOut = nullptr )
{
	AsciiString full;
	full.format("%s.wnd:%s", layout, name);
	const NameKeyType id = TheNameKeyGenerator->nameToKey(full);
	if (idOut)
		*idOut = id;
	return TheWindowManager ? TheWindowManager->winGetWindowFromId(parent, id) : nullptr;
}

// The string table entry, or an English fallback while the mod's strings do not have it yet.
UnicodeString labelText( const char *label, const WideChar *fallback )
{
	if (TheGameText == nullptr)
		return UnicodeString(fallback);
	return TheGameText->FETCH_OR_SUBSTITUTE(label, fallback);
}

// A choice's Name is its label; without a string table entry the part after the last ':' is shown.
UnicodeString choiceText( const AsciiString &name )
{
	const char *tail = name.reverseFind(':');
	AsciiString shortName = tail ? AsciiString(tail + 1) : name;
	UnicodeString fallback;
	fallback.translate(shortName);
	if (TheGameText == nullptr)
		return fallback;
	return TheGameText->FETCH_OR_SUBSTITUTE(name.str(), fallback.str());
}

void setLabel( GameWindow *label, const char *key, const WideChar *fallback )
{
	if (label)
		GadgetStaticTextSetText(label, labelText(key, fallback));
}

Int addEntry( GameWindow *combo, const UnicodeString &text, Int index )
{
	const Int pos = GadgetComboBoxAddEntry(combo, text, combo->winGetEnabled() ? combo->winGetEnabledTextColor() : combo->winGetDisabledTextColor());
	GadgetComboBoxSetItemData(combo, pos, (void *)(intptr_t)index);
	return pos;
}

void selectIndex( GameWindow *combo, Int index )
{
	if (combo == nullptr)
		return;
	const Int count = GadgetComboBoxGetLength(combo);
	for (Int pos = 0; pos < count; ++pos)
	{
		if ((Int)(intptr_t)GadgetComboBoxGetItemData(combo, pos) == index)
		{
			Int current = -1;
			GadgetComboBoxGetSelectedPos(combo, &current);
			if (current != pos)
				GadgetComboBoxSetSelectedPos(combo, pos, TRUE);
			return;
		}
	}
}

// The index behind the combo's selection, or -1.
Int selectedIndex( GameWindow *combo )
{
	if (combo == nullptr)
		return -1;
	Int pos = -1;
	GadgetComboBoxGetSelectedPos(combo, &pos);
	if (pos < 0 || pos >= GadgetComboBoxGetLength(combo))
		return -1;
	return (Int)(intptr_t)GadgetComboBoxGetItemData(combo, pos);
}

void hideRow( GameWindow *label, GameWindow *control, Bool hide )
{
	if (label)
		label->winHide(hide);
	if (control)
		control->winHide(hide);
}

} // namespace

// ------------------------------------------------------------------------------------------------
void AoCLobbyGadgets::clear()
{
	stockLabel = nullptr;
	stockCombo = nullptr;
	richnessLabel = nullptr;
	richnessCombo = nullptr;
	capLabel = nullptr;
	capCombo = nullptr;
	victoryLabel = nullptr;
	victoryCombo = nullptr;
	ambientCheck = nullptr;
	stockComboID = NAMEKEY_INVALID;
	richnessComboID = NAMEKEY_INVALID;
	capComboID = NAMEKEY_INVALID;
	victoryComboID = NAMEKEY_INVALID;
	ambientCheckID = NAMEKEY_INVALID;
	stockShowsChoices = FALSE;
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCLobbyUI::findGadgets( GameWindow *parent, const char *layout, AoCLobbyGadgets &g )
{
	g.clear();
	g.stockLabel = findChild(parent, layout, "StaticTextAoCStartingStock");
	g.stockCombo = findChild(parent, layout, "ComboBoxStartingCash", &g.stockComboID);
	g.richnessLabel = findChild(parent, layout, "StaticTextAoCRichness");
	g.richnessCombo = findChild(parent, layout, "ComboBoxAoCRichness", &g.richnessComboID);
	g.capLabel = findChild(parent, layout, "StaticTextAoCPopulationCap");
	g.capCombo = findChild(parent, layout, "ComboBoxAoCPopulationCap", &g.capComboID);
	g.victoryLabel = findChild(parent, layout, "StaticTextAoCVictory");
	g.victoryCombo = findChild(parent, layout, "ComboBoxAoCVictory", &g.victoryComboID);
	g.ambientCheck = findChild(parent, layout, "CheckboxAoCAmbientCivilians", &g.ambientCheckID);
}

// ------------------------------------------------------------------------------------------------
/*static*/ Bool AoCLobbyUI::replacesStartingCash()
{
	return AoCLobbyRules::hasStartingStockList();
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCLobbyUI::populate( AoCLobbyGadgets &g, const GameInfo *game )
{
	const Bool any = AoCLobbyRules::anyListDefined();

	// Starting Stock in place of Starting Cash.
	if (replacesStartingCash() && g.stockCombo)
	{
		GadgetComboBoxReset(g.stockCombo);
		for (Int i = 0; i < TheAoCLobbyRules->getStartingStockCount(); ++i)
			addEntry(g.stockCombo, choiceText(TheAoCLobbyRules->getStartingStock(i)->name), i);
		g.stockShowsChoices = TRUE;
		setLabel(g.stockLabel, "GUI:AoCStartingStock", L"Starting Stock");
	}
	else if (g.stockLabel)
	{
		// the mod's layout with stock data: the label goes back to the stock text
		GadgetStaticTextSetText(g.stockLabel, labelText("GUI:StartingMoney", L"Starting Cash"));
	}

	if (g.richnessCombo)
	{
		GadgetComboBoxReset(g.richnessCombo);
		for (Int i = 0; i < (TheAoCLobbyRules ? TheAoCLobbyRules->getRichnessCount() : 0); ++i)
			addEntry(g.richnessCombo, choiceText(TheAoCLobbyRules->getRichness(i)->name), i);
	}
	setLabel(g.richnessLabel, "GUI:AoCRichness", L"Richness");
	hideRow(g.richnessLabel, g.richnessCombo, !AoCLobbyRules::hasRichnessList());

	if (g.capCombo)
	{
		GadgetComboBoxReset(g.capCombo);
		for (Int i = 0; i < (TheAoCLobbyRules ? TheAoCLobbyRules->getPopulationCapCount() : 0); ++i)
			addEntry(g.capCombo, choiceText(TheAoCLobbyRules->getPopulationCap(i)->name), i);
	}
	setLabel(g.capLabel, "GUI:AoCPopulationCap", L"Population");
	hideRow(g.capLabel, g.capCombo, !AoCLobbyRules::hasPopulationCapList());

	// Victory: conquest only until wonder and score exist (D6 section 7: those entries stay out until then).
	if (g.victoryCombo)
	{
		GadgetComboBoxReset(g.victoryCombo);
		addEntry(g.victoryCombo, labelText(AoCLobbyRules::victoryLabel(AOC_VICTORY_CONQUEST), L"Conquest"), AOC_VICTORY_CONQUEST);
	}
	setLabel(g.victoryLabel, "GUI:AoCVictory", L"Victory");
	hideRow(g.victoryLabel, g.victoryCombo, !any);

	if (g.ambientCheck)
	{
		g.ambientCheck->winSetText(labelText("GUI:AoCAmbientCivilians", L"Ambient Civilians"));
		g.ambientCheck->winHide(!any);
	}

	show(g, game);
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCLobbyUI::show( AoCLobbyGadgets &g, const GameInfo *game )
{
	if (game == nullptr)
		return;
	if (g.stockShowsChoices)
		selectIndex(g.stockCombo, game->getAoCStartingStock());
	selectIndex(g.richnessCombo, game->getAoCRichness());
	selectIndex(g.capCombo, game->getAoCPopulationCap());
	selectIndex(g.victoryCombo, game->getAoCVictory());
	if (g.ambientCheck && GadgetCheckBoxIsChecked(g.ambientCheck) != game->getAoCAmbientCivilians())
		GadgetCheckBoxSetChecked(g.ambientCheck, game->getAoCAmbientCivilians());
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCLobbyUI::enable( AoCLobbyGadgets &g, Bool enabled )
{
	// the stock combo follows the stock code's own enable call
	if (g.richnessCombo)
		g.richnessCombo->winEnable(enabled);
	if (g.capCombo)
		g.capCombo->winEnable(enabled);
	if (g.victoryCombo)
		g.victoryCombo->winEnable(enabled);
	if (g.ambientCheck)
		g.ambientCheck->winEnable(enabled);
}

// ------------------------------------------------------------------------------------------------
/*static*/ Bool AoCLobbyUI::handleControl( AoCLobbyGadgets &g, Int controlID, GameInfo *game, Bool &changed )
{
	changed = FALSE;
	if (controlID == NAMEKEY_INVALID)
		return FALSE;

	AoCLobbySettings s = game ? game->getAoCLobbySettings() : AoCLobbySettings();
	const AoCLobbySettings before = s;

	if (g.stockShowsChoices && g.stockCombo && controlID == g.stockComboID)
	{
		const Int index = selectedIndex(g.stockCombo);
		if (index >= 0)
			s.startingStock = index;
	}
	else if (g.richnessCombo && controlID == g.richnessComboID)
	{
		const Int index = selectedIndex(g.richnessCombo);
		if (index >= 0)
			s.richness = index;
	}
	else if (g.capCombo && controlID == g.capComboID)
	{
		const Int index = selectedIndex(g.capCombo);
		if (index >= 0)
			s.populationCap = index;
	}
	else if (g.victoryCombo && controlID == g.victoryComboID)
	{
		const Int index = selectedIndex(g.victoryCombo);
		if (index >= 0)
			s.victory = index;
	}
	else if (g.ambientCheck && controlID == g.ambientCheckID)
	{
		s.ambientCivilians = GadgetCheckBoxIsChecked(g.ambientCheck);
	}
	else
	{
		return FALSE;
	}

	if (game && s != before)
	{
		game->setAoCLobbySettings(s);	// with a stock list, the chosen stock's cash becomes the starting cash too
		changed = TRUE;
	}
	return TRUE;
}
