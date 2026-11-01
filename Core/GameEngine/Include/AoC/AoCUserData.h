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

// AoCUserData.h
// Age of Commanders F12a (design D11 section 2.12, harness): the per-run user-data folder of -aocUserData <dir>.
// The pure part, so the unit tests can run it: how the flag's argument becomes the folder the engine stores as its
// user-data path (GlobalData::getPath_UserData, which every save, replay, preference file, map cache, screenshot and
// crash file is built from). AoCHarness::parseAocUserData creates the folder and installs it.
//
// The engine's own path is absolute and ends with one separator ("C:\Users\x\Documents\Command and Conquer Generals
// Zero Hour Data\"), and every caller appends a file or folder name to it, so the override takes the same shape:
//   - a relative argument is taken against the base folder (the process's working directory when the flag is parsed,
//     which is before -setCwd changes it);
//   - "." segments, repeated separators and ".." segments are resolved ("..": the segment before it is dropped; a ".."
//     at the root stays at the root);
//   - Windows rules: '/' and '\' are both separators on input, '\' on output; absolute means a drive and a separator
//     ("C:\x") or a UNC share ("\\server\share\x"); a rooted path without a drive ("\x") takes the base's drive; a
//     drive-relative path ("C:x") is refused, since its meaning depends on a per-drive current folder;
//   - POSIX rules: '/' only (a backslash is an ordinary character); absolute means a leading '/';
//   - exactly one trailing separator.
// An empty or blank argument, or a relative one without a usable base, is refused.

#pragma once

#include "Lib/BaseType.h"

#include <string>

namespace AoCUserData
{
	/// The folder an -aocUserData argument names, by the rules above (windowsRules selects the Windows or the POSIX
	/// set). Returns TRUE and the folder in out, or FALSE with out empty. Touches no file.
	Bool resolveFor( const char *arg, const char *baseDir, Bool windowsRules, std::string &out );

	/// resolveFor with the rules of the platform the game was built for.
	Bool resolve( const char *arg, const char *baseDir, std::string &out );

	/// The longest folder -aocUserData accepts, its trailing separator included. The engine appends file and folder
	/// names to the user-data path in fixed buffers of _MAX_PATH (260) bytes: W3DFileSystem sprintf()s the user art and
	/// map preview folders into its m_filePath[_MAX_PATH] (USER_W3D_DIR_PATH, USER_TGA_DIR_PATH, MAP_PREVIEW_DIR_PATH)
	/// and then a file name, and the narrow Win32 calls fail past 260 characters. 180 leaves room for
	/// "Maps\<name>\<name>.map", "Save\<name>.sav" and "MapPreviews\<long name>.tga".
	enum { MAX_FOLDER_LENGTH = 180 };

	/// Why prepare() refused a folder (each ends the process with exit code 6).
	enum PrepareResult
	{
		PREPARE_OK,
		PREPARE_NO_ARGUMENT,		///< no argument after the flag
		PREPARE_BAD_NAME,				///< resolve() refused it
		PREPARE_TOO_LONG,				///< longer than MAX_FOLDER_LENGTH after resolution
		PREPARE_NOT_A_FOLDER,		///< it could not be created, or it names something that is not a folder (a file)
		PREPARE_NOT_WRITABLE,		///< a file could not be created in it (a read-only folder)
	};

	/// Everything -aocUserData does before it installs the folder: resolve() against baseDir, the length limit, create
	/// the folder (and its parents) when missing, check it is a folder, and probe write access by creating and deleting a
	/// file in it. PREPARE_OK and the folder (with its trailing separator) in out, or the reason and out empty.
	PrepareResult prepare( const char *arg, const char *baseDir, std::string &out );

	/// A short English reason for a PrepareResult, for the stderr line.
	const char *describe( PrepareResult result );

	/// The instance mutex name (rts::ClientInstance) of a game with its own user-data folder: the stock name, then
	/// "-AoCUserData-" and 16 hex digits of a 64-bit FNV-1a hash of the folder with ASCII letters folded to lower case
	/// (Windows paths ignore case; a mutex name cannot hold a backslash). The stock rule "one game at a time" becomes
	/// "one game per user-data folder": runs with different folders start beside each other and beside a game without
	/// the flag, and keep the stock file names of the first instance (Options.ini, not Options_Instance02.ini).
	std::string instanceName( const char *firstInstanceName, const char *folder );
}
