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

// AoCFolder.cpp
// Age of Commanders F5: command bar folders. See AoCFolder.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCFolder.h"

#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/Object.h"

// --- data helpers (logic and client alike; command set data only) ----------------------------------

Bool AoCFolder::isFolderCommand( const CommandButton *button )
{
	return button != nullptr &&
		(button->getCommandType() == GUI_COMMAND_AOC_FOLDER || button->getCommandType() == GUI_COMMAND_AOC_FOLDER_BACK);
}

const CommandSet *AoCFolder::folderSetOf( const CommandButton *button )
{
	if( button == nullptr || button->getCommandType() != GUI_COMMAND_AOC_FOLDER || button->getAoCFolderSet().isEmpty() )
		return nullptr;
	return TheControlBar ? TheControlBar->findCommandSet( button->getAoCFolderSet() ) : nullptr;
}

const CommandButton *AoCFolder::findBuildCommandInFolders( const CommandSet *set, const ThingTemplate *whatToBuild )
{
	if( set == nullptr || whatToBuild == nullptr )
		return nullptr;
	const CommandButton *found = nullptr;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandSet *folder = folderSetOf( set->getCommandButton( i ) );
		if( folder == nullptr )
			continue;
		// one level deep, the same test as the stock scan of the object's own set (the last match wins there too)
		for( Int j = 0; j < MAX_COMMANDS_PER_SET; ++j )
		{
			const CommandButton *button = folder->getCommandButton( j );
			if( button &&
					(button->getCommandType() == GUI_COMMAND_UNIT_BUILD || button->getCommandType() == GUI_COMMAND_DOZER_CONSTRUCT) &&
					button->getThingTemplate() && button->getThingTemplate()->isEquivalentTo( whatToBuild ) )
				found = button;
		}
	}
	return found;
}

Bool AoCFolder::hasUpgradeCommandInFolders( const CommandSet *set, const UpgradeTemplate *upgrade )
{
	if( set == nullptr || upgrade == nullptr )
		return FALSE;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
	{
		const CommandSet *folder = folderSetOf( set->getCommandButton( i ) );
		if( folder == nullptr )
			continue;
		for( Int j = 0; j < MAX_COMMANDS_PER_SET; ++j )
		{
			const CommandButton *button = folder->getCommandButton( j );
			if( button && button->getUpgradeTemplate() && button->getUpgradeTemplate() == upgrade )
				return TRUE;
		}
	}
	return FALSE;
}

// --- ControlBar view state (client only) ---------------------------------------------------------

void ControlBar::aocCloseFolder()
{
	m_aocOpenFolder.clear();
	m_aocFolderOwner = INVALID_ID;
	m_aocFolderBaseSet.clear();
	m_aocFolderButton = nullptr;
}

void ControlBar::aocOpenFolder( const CommandButton *folderButton, Object *obj )
{
	// a folder belongs to one object: in a multi-selection (no object) the button does nothing. Folders are
	// one level deep (D6 3.2), as the logic's folder lookups are: a folder button inside a folder does nothing.
	if( obj == nullptr || m_aocOpenFolder.isNotEmpty() || AoCFolder::folderSetOf( folderButton ) == nullptr )
		return;
	m_aocOpenFolder = folderButton->getAoCFolderSet();
	m_aocFolderOwner = obj->getID();
	m_aocFolderBaseSet = obj->getCommandSetString();
	m_aocFolderButton = folderButton;
	markUIDirty();
}

Bool ControlBar::aocFolderBack()
{
	if( m_aocOpenFolder.isEmpty() )
		return FALSE;
	// Backspace acts as a press of Back, and every command button press ends a pending placement (processCommandUI)
	if( TheInGameUI->getPendingPlaceType() != nullptr )
		TheInGameUI->placeBuildAvailable( nullptr, nullptr );
	aocCloseFolder();
	markUIDirty();
	return TRUE;
}

const AsciiString& ControlBar::aocShownCommandSet() const
{
	return m_currContext == CB_CONTEXT_COMMAND ? m_aocShownSet : AsciiString::TheEmptyString;
}

void ControlBar::aocCheckFolderOwner()
{
	if( m_aocOpenFolder.isEmpty() )
		return;
	// the object evaluateContextUI goes on to show: the one selected drawable, or an angry mob's nexus
	Drawable *draw = nullptr;
	if( TheInGameUI->getSelectCount() == 1 && !TheInGameUI->getAllSelectedDrawables()->empty() )
		draw = TheInGameUI->getAllSelectedDrawables()->front();
	else if( TheInGameUI->getSelectCount() > 1 )
		draw = TheGameClient->findDrawableByID( TheInGameUI->getSoloNexusSelectedDrawableID() );
	const Object *obj = draw ? draw->getObject() : nullptr;
	if( obj == nullptr || obj->getID() != m_aocFolderOwner )
		aocCloseFolder();
}

const CommandSet *ControlBar::aocCommandSetFor( Object *obj )
{
	const CommandSet *set = nullptr;
	if( m_aocOpenFolder.isNotEmpty() )
	{
		if( obj->getID() == m_aocFolderOwner && obj->getCommandSetString() == m_aocFolderBaseSet )
			set = findCommandSet( m_aocOpenFolder );
		if( set == nullptr )
			aocCloseFolder();	// the owner's own set changed (CommandSetUpgrade)
	}
	if( set == nullptr )
		set = findCommandSet( obj->getCommandSetString() );	// the stock line
	m_aocShownSet = set ? set->getName() : AsciiString::TheEmptyString;
	return set;
}

void ControlBar::aocSetCaption( GameWindow *button, const CommandButton *commandButton )
{
	Bool commandWindow = FALSE;
	for( Int i = 0; i < MAX_COMMANDS_PER_SET; ++i )
		if( m_commandWindows[ i ] == button )
			commandWindow = TRUE;
	if( !commandWindow )
		return;

	if( !AoCFolder::isFolderCommand( commandButton ) )
	{
		// stock never puts text on a command button that has a label: remove only what a folder left there
		if( button->winGetTextLength() > 0 )
			GadgetButtonSetText( button, UnicodeString::TheEmptyString );
		return;
	}

	// Back names the open folder (the breadcrumb), a folder names itself; both without the '&' hotkey marker
	const Bool back = commandButton->getCommandType() == GUI_COMMAND_AOC_FOLDER_BACK && m_aocFolderButton != nullptr;
	const CommandButton *named = back ? m_aocFolderButton : commandButton;
	UnicodeString label = TheGameText->fetch( named->getTextLabel() );
	UnicodeString caption;
	for( Int i = 0; i < label.getLength(); ++i )
		if( label.getCharAt( i ) != L'&' )
			caption.concat( label.getCharAt( i ) );
	if( back )
	{
		caption.trimEnd( L'>' );
		caption.trimEnd();
	}
	GadgetButtonSetText( button, caption );
}
