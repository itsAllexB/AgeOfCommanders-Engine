/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

#include "PreRTS.h"
#include "GameClient/GameText.h"
#include "GameClient/InGameUI.h"
#include "GameClient/MessageBox.h"
#include "GameClient/SaveLoadFeedback.h"
#include "AoC/AoCSimRate.h"

static UnicodeString getUnicodeSavePath( const AsciiString &filename )
{
	UnicodeString path;
	path.translate( TheGameState->getFilePathInSaveDirectory(filename) );
	return path;
}

void presentSaveResult( const SaveResult &result )
{
	switch( result.saveCode )
	{
		case SC_OK:
		{
			TheInGameUI->message( TheGameText->fetch("GUI:GameSaveComplete") );
			break;
		}
		case SC_UNABLE_TO_OPEN_FILE:
		{
			TheInGameUI->message( "GUI:Error" );
			break;
		}
		case SC_ERROR:
		{
			UnicodeString msg;
			msg.format( TheGameText->fetch("GUI:ErrorSavingGame"), getUnicodeSavePath(result.filename).str() );
			MessageBoxOk( TheGameText->fetch("GUI:Error"), msg, nullptr );
			break;
		}
		default:
		{
			break;
		}
	}
}

void presentLoadResult( SaveCode result, const AsciiString &filename )
{
	if( result == SC_INVALID_DATA )
	{
		UnicodeString msg;
		msg.format( TheGameText->fetch("GUI:ErrorLoadingGame"), getUnicodeSavePath(filename).str() );
		MessageBoxOk( TheGameText->fetch("GUI:Error"), msg, nullptr );
	}
	else if( result == SC_AOC_SIMRATE_MISMATCH )
	{
		// AoC @hook 30/10/2026 The save was made at the other simulation rate (D11 F15).
		MessageBoxOk( TheGameText->fetch("GUI:Error"), AoCSimRate::mismatchMessage("This saved game", AoCSimRate::lastSaveMismatchRate()), nullptr );
	}
}
