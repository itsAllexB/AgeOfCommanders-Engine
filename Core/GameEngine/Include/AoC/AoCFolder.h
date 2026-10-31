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

// AoCFolder.h
// Age of Commanders: command bar folders (design D11 F5, D6 section 3).
//
// Data:
//
//   CommandButton Command_AoCFolderDefence
//     Command          = AOC_FOLDER
//     FolderCommandSet = AoC_AmericaDozerDefence    ; the set the button shows for the same selection
//     TextLabel        = CONTROLBAR:AoCFolderDefence ; "&Defence >": hotkey D, caption "Defence >"
//   End
//   CommandButton Command_AoCFolderBack
//     Command   = AOC_FOLDER_BACK                   ; back to the object's own set; slot 14 of a folder
//     TextLabel = CONTROLBAR:AoCBack
//   End
//   CommandMap AOC_FOLDER_BACK                      ; Backspace, the meta command MSG_META_AOC_FOLDER_BACK
//     Key = KEY_BACKSPACE
//     ...
//
// The folder is client view state in TheControlBar (the ControlBar::aoc* members, defined in AoCFolder.cpp): it
// appends no GameMessage, is never saved, and closes when the selection changes to another object, when the
// owner's command set string changes (CommandSetUpgrade), on Back and at a new game. A placement started from a
// folder keeps it open, because neither placing nor cancelling (right click) deselects the builder. Folders and
// Back are always enabled. A multi-selection shows the stock common commands; a folder needs one object.
// Captions: the W3D image push-button draw renders the window text over the cameo, centred, so folder buttons
// get their label (without '&') as text and Back gets the open folder's name (D6 3.2 breadcrumb).
//
// The logic checks that a builder or factory has a button for what it is asked to make
// (BuildAssistant::isPossibleToMakeUnit, Object::canProduceUpgrade). A button inside a folder counts as the
// object's own: the helpers below look one folder level deep in the command set data, which every machine loads
// alike; they never read the view state. With stock data no button has a FolderCommandSet and they find nothing.

#pragma once

class CommandButton;
class CommandSet;
class ThingTemplate;
class UpgradeTemplate;

class AoCFolder
{
public:
	/// The set an AOC_FOLDER button opens, or nullptr for any other button or an unknown set name.
	static const CommandSet *folderSetOf( const CommandButton *button );

	/// The UNIT_BUILD or DOZER_CONSTRUCT button for whatToBuild inside the folders that 'set' opens, or nullptr.
	static const CommandButton *findBuildCommandInFolders( const CommandSet *set, const ThingTemplate *whatToBuild );

	/// TRUE when a folder that 'set' opens holds a button for 'upgrade'.
	static Bool hasUpgradeCommandInFolders( const CommandSet *set, const UpgradeTemplate *upgrade );

	/// TRUE for AOC_FOLDER and AOC_FOLDER_BACK buttons.
	static Bool isFolderCommand( const CommandButton *button );
};
