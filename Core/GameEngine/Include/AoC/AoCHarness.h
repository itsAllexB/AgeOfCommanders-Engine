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
//                                   e.g. NO_HOUSING:12) and blocked_units (the templates refused for
//                                   housing since the previous row with the population each needs, its
//                                   initial payload included, e.g. ChinaVehicleTroopCrawler(9), up to 8
//                                   names, |-separated), then (F2) workers_<Type> (gatherers whose assigned
//                                   type is that type) and workers_idle (gatherers neither ferrying nor
//                                   doing anything else), then (F3) converters_running and converters_idle (the
//                                   player's AoCConverterUpdate modules by AoCConverterInterface::isRunning). With
//                                   -aiLog, stderr also gets each AI player's
//                                   population when -saveAtFrame saves and at the first frame of a loaded
//                                   game, and, at a row where used + reserved is above the cap, what the
//                                   population is made of (template x count).
//   (any flag)                      F2: the typed gather path prints one stderr line per event while any harness
//                                   flag is given, "[AoC gather] frame N <objectId> <event>": picked, deposited,
//                                   dropped, depleted, refused. Nothing in a stock run; logic never reads it.
//                                   F3: a converter prints "[AoC convert] frame N <objectId> ran <input> -> <output>
//                                   bank <Type>=<n>... player <index> [<condition> on|off]" at each cycle end that converts
//                                   and "... idle <reason> bank ..." (no-input, no-power, under-construction, sold, neutral, disabled)
//                                   once per change of reason. Nothing in a stock run; logic never reads it.
//   -saveAtFrame <N> <name>         F1: write the stock save game <name>.sav (the save menu's path,
//                                   TheGameState->saveGame) once logic frame N (1 or more) has run, from the
//                                   engine loop. Load it with the stock -loadsave <name>.sav at the same
//                                   -simrate (a save made at the other rate is refused, exit code 5).
//   -aocOptions "<key=value;...>"   F7: options string keys appended to the string -skirmish builds, after its fixed keys
//                                   and before the slot list, e.g. "AO=220N0" (the lobby fields in one key: starting
//                                   stock, richness and population cap as one hex digit each, indices into the INI
//                                   choice lists; ambient civilians Y or N; the victory digit). Parsed by the stock
//                                   parser, so an unknown key or a malformed value stops -skirmish (exit code 4). Ignored without
//                                   -skirmish. With any harness flag and lobby choice lists or resource types defined,
//                                   stderr gets at the start of a game "[AoC lobby] stock=<name or index> richness=<name>
//                                   cap=<n> ambient=<Y|N> victory=<name>" and "[AoC lobby] richness scaled <n>
//                                   warehouses (<before> to <after> boxes in the first)", and the first line again at
//                                   the first frame of a loaded game. Nothing is printed with stock data.
//   -simrate <30|60>                Simulation rate for this process (D11 F15), overriding the
//                                   Options.ini SimulationRate key. Any other value exits with code 3.
//   -aocUiScript <file>             F5: client UI steps for unattended UI tests. Lines "<frame> <command> [arg]"
//                                   in frame order (others are comments), up to 64; each runs in the engine loop
//                                   once logic frame <frame> has run, through the path a click or key takes:
//                                     select <Template>  the local player's object of that template with the
//                                                        lowest id, selected as a left click selects it
//                                     press <slot>       the visible, enabled command button 1 to 14 (GBM_SELECTED,
//                                                        as a click or its hotkey); a hidden or disabled one is not
//                                     back               Backspace down and up into the message stream
//                                     log                stderr "[AoC ui] frame <frame> set=<shown command set or
//                                                        -> slots=<button per slot 1..14, - when not shown>"
//                                                        plus " place=<template>" while a placement is pending
//                                     screenshot <file>  the engine's Release PNG screenshot (Ctrl+F12 path),
//                                                        moved from <user data>/Screenshots to <file>
//                                     selectall <Template> F2: every object of the local player with that
//                                                        template, selected in one stock selection message
//                                     dock <Template>    F2: the stock dock order (MSG_DOCK, a right-click's) of
//                                                        the selection on the object of that template nearest
//                                                        the first selected object (lowest id)
//                                     bank               F2: stderr "[AoC bank] frame <frame> logic <frame run>
//                                                        <Type>=<n>... cargo <id> <Type>:<n>/<capacity> asg <Type>
//                                                        at <x>,<y> ai <state type> goal <id> to <x>,<y>;" (asg: the
//                                                        assigned type, - when none) with the local
//                                                        player's bank and one cargo segment per selected object,
//                                                        the first selected (lowest id) first
//                                     count <Template>   F2: stderr "[AoC count] frame <frame> logic <frame run>
//                                                        <Template> <n>", the live objects of that template
//                                   After select, selectall, press, back and dock the next line waits 3 engine
//                                   iterations so the control bar re-evaluates. Use absolute paths. select, selectall
//                                   and dock send the stock message a click sends, so they move the CRC and the
//                                   replay records them; press, back, log, bank, count and screenshot send nothing. Each of
//                                   select, selectall and dock must be the first line of its frame and come after the
//                                   lines before it have settled: a late one (warned on stderr) lands on a frame
//                                   that depends on the render rate, and the CRC log is no longer reproducible.
//                                   Refused under -headless (no windows). Over 64 lines or a line over 511
//                                   characters is reported on stderr and skipped.
//
// Exit codes: 0 normal, 2 CRC mismatch seen, 3 unsupported -simrate, 4 -skirmish could not start,
// 5 a replay or save made at the other simulation rate was refused.
//
// Everything lives in this class; stock files only gain one hook line each (see the "Hooks" section).
// The harness never changes logic state and never branches on the local player, so stock replay CRCs
// are unchanged whether or not the flags are given.

#pragma once

class Player;
class ThingTemplate;

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
	static Int parseAocUiScript( char *args[], int num );	// AoC F5
	static Int parseAocOptions( char *args[], int num );	// AoC F7

	/// TRUE when any harness flag was given on the command line.
	static Bool isActive();

	// Hooks, one call each in a stock file:
	/// GameEngine::init, right after the RTS_DEBUG-only -file block: builds TheSkirmishGameInfo and sends MSG_NEW_GAME.
	static void startSkirmishIfRequested();
	/// GameLogic::update, right after the stock MSG_LOGIC_CRC block: CRC log, AI log, quit request.
	static void onLogicFrame( UnsignedInt frame, Bool crcGenerated, UnsignedInt crc );
	/// RecorderClass::handleCRCMessage, where a replay CRC mismatch is reported.
	static void onCRCMismatch( UnsignedInt frame, UnsignedInt inGameCRC, UnsignedInt replayCRC );
	/// GameEngine::execute, after each update(): runs due -aocUiScript lines, writes the save requested by -saveAtFrame, performs the clean quit requested by -quitAtFrame.
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
	static void onUnitQueueRefused( const Player *player, Int canMakeType, const ThingTemplate *unitType );

	/// GameLogic::startNewGame, at the end of a new game's start (not a load, not the shell): the "[AoC lobby]" lines with the
	/// lobby fields in effect and what richness did (D11 F7). Reads only; nothing without a harness flag or with stock data.
	static void onLobbyGameStart( Int richnessPerMille, Int warehousesScaled, Int firstBefore, Int firstAfter );
};
