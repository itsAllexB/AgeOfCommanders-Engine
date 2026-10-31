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

// AoCHarness.h
// Age of Commanders: unattended-testing harness (design D11 F12, D9 section 7).
//
// Command-line flags, all inert when absent, available in Release builds:
//
//   -skirmish <map> <slots> <seed>  Start a skirmish straight from the command line, without the
//                                   shell. <map> is anything TheMapCache->findMap resolves (a user map
//                                   folder name such as Flat4_seed1, or a path). <slots> is the replay
//                                   slot-list syntax of GameInfoToAsciiString (":"-separated, up to 8
//                                   entries, missing entries are closed). Slot 0 must be the human
//                                   local player, who stays idle; AI slots are C<E|M|H>,color,template,
//                                   startPos,team. <seed> seeds the logic random number generator.
//   -quitAtFrame <N>                Leave cleanly at logic frame N. Exit code 0, or 2 if a replay CRC
//                                   mismatch was detected during the run.
//   -crcLog <file>                  Append one "frame,crc" line per logic CRC interval, from live games
//                                   and replays alike (the stock GameLogic::getCRC values).
//   -aiLog <dir>                    Every 900 logic frames write one CSV row per AI player to
//                                   <dir>/ai_<playerIndex>.csv. Logic reads only. With resource types
//                                   defined (F1) the row gains bank_<Type> columns and cash_equivalent,
//                                   then (F4) pop_used, pop_capacity, pop_reserved, pop_cap (capacity
//                                   clamped to the lobby cap: what production is checked against) and
//                                   blocked_build (unit queue refusals since the previous row, by reason,
//                                   e.g. NO_HOUSING:12). With -aiLog, stderr also gets each AI player's
//                                   population when -saveAtFrame saves and at the first frame of a loaded
//                                   game, and, at a row where used + reserved is above the cap, what the
//                                   population is made of (template x count).
//   -saveAtFrame <N> <name>         F1: write the stock save game <name>.sav (the save menu's path,
//                                   TheGameState->saveGame) once logic frame N (1 or more) has run, from the
//                                   engine loop. Load it with the stock -loadsave <name>.sav at the same
//                                   -simrate (a save made at the other rate is refused, exit code 5).
//   -simrate <30|60>                Simulation rate for this process (D11 F15), overriding the
//                                   Options.ini SimulationRate key. Any other value exits with code 3.
//
// Exit codes: 0 normal, 2 CRC mismatch seen, 3 unsupported -simrate, 4 -skirmish could not start,
// 5 a replay or save made at the other simulation rate was refused.
//
// Everything lives in this class; stock files only gain one hook line each (see the "Hooks" section).
// The harness never changes logic state and never branches on the local player, so stock replay CRCs
// are unchanged whether or not the flags are given.

#pragma once

class Player;

class AoCHarness
{
public:

	// Command-line table handlers (Core/GameEngine/Source/Common/CommandLine.cpp).
	// Each returns the number of arguments it consumed, like every other parse function there.
	static Int parseSkirmish( char *args[], int num );
	static Int parseQuitAtFrame( char *args[], int num );
	static Int parseCrcLog( char *args[], int num );
	static Int parseAiLog( char *args[], int num );
	static Int parseSimRate( char *args[], int num );
	static Int parseSaveAtFrame( char *args[], int num );	// AoC F1

	/// TRUE when any harness flag was given on the command line.
	static Bool isActive();

	// Hooks, one call each in a stock file:
	/// GameEngine::init, right after the RTS_DEBUG-only -file block: builds TheSkirmishGameInfo and sends MSG_NEW_GAME.
	static void startSkirmishIfRequested();
	/// GameLogic::update, right after the stock MSG_LOGIC_CRC block: CRC log, AI log, quit request.
	static void onLogicFrame( UnsignedInt frame, Bool crcGenerated, UnsignedInt crc );
	/// RecorderClass::handleCRCMessage, where a replay CRC mismatch is reported.
	static void onCRCMismatch( UnsignedInt frame, UnsignedInt inGameCRC, UnsignedInt replayCRC );
	/// GameEngine::execute, after each update(): writes the save requested by -saveAtFrame, performs the clean quit requested by -quitAtFrame.
	static void pollQuit();
	/// GameMain, before returning: maps the harness state to the process exit code.
	static int finalExitCode( int exitcode );

	/// RecorderClass::playbackFile / GameLogic::xfer: a replay or save at the other simulation rate was refused (D11 F15).
	/// Under -quitAtFrame the process leaves with exit code 5 instead of idling in the shell.
	static void onSimRateRefusal();

	/// GameEngine::update, around TheGameLogic->UPDATE(): logic frame timing for -aiLog's timing.csv (D11 F15 test e).
	static void logicUpdateBegin();
	static void logicUpdateEnd();

	/// ProductionUpdate::queueCreateUnit refused a unit (a CanMakeType other than CANMAKE_OK): counted per player for
	/// the -aiLog blocked_build column (D11 F4). Harness state only; the logic never reads it.
	static void onUnitQueueRefused( const Player *player, Int canMakeType );
};
