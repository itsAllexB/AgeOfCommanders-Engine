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
//                                   player's AoCConverterUpdate modules by AoCConverterInterface::isRunning), then (F9)
//                                   wanderers (live objects whose AI module is AoCWanderAIUpdate), then (F8)
//                                   builders_busy (dozers and workers whose current dozer task is a build, walking to the
//                                   site or constructing). With -aiLog, stderr
//                                   also gets each AI player's
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
//                                   F9 (AoCWanderAIUpdate): "[AoC wander] frame N <objectId> dist=<d> dest=<d>
//                                   pause=<frames> max=<m>" per ended stroll (dist: where the walker stands, dest:
//                                   the destination issued, both from the anchor; max: the farthest a pick can lie,
//                                   the anchor's clearance plus WanderRadius), "... pick failed pause=<frames>
//                                   max=<m>" when no draw gave a usable destination, "... unstuck dist=<d> max=<m>"
//                                   when the watchdog takes over a walker locked in a stock move.
//                                   F8 (AoCBuildSpeed), with resource types defined (nothing with stock data):
//                                   "[AoC build] frame N <builderId> start <template> <objectId>
//                                   speed=<perMille>" when a builder starts constructing (its first construction frame on
//                                   the structure, or its first after a gap; " resumed progress=<percent>" on a structure
//                                   with progress and no start line in this process, as after a load), "[AoC build] frame N
//                                   <objectId> complete <template> builders=<n> frames=<k> base=<framesToBuild>" (n distinct
//                                   builders since the first start line, k frames from that line to this one, both
//                                   counted; base: calcTimeToBuild on the last frame), "[AoC build] frame N <builderId>
//                                   back-to-work <type>" when a worker's build ends and it returns to its assigned type,
//                                   "... back-to-work none in range <type> range=<r>" when no node of that type stands
//                                   within GameData.ini AoCBackToWorkRange of the build site and it idles.
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
//   -crcDump <frame>|<first>:<last> <file>
//                                   F14a: write a text decomposition of the logic CRC at that frame (or at every frame
//                                   of the range) to <file>: the frame's CRC against the same sequence recomputed, the
//                                   four sections of GameLogic::getCRC, one line per object with its Object::crc and
//                                   the parts it covers, and at the single (or last) frame each module's crc, each
//                                   player, each row of partition cells and the AI's pathfinder, data and groups
//                                   (AoCCrcDump.h). Read only: crc functions and getters, never a save-mode xfer.
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
//                                                        the first selected (lowest id) first; F8: a segment ends
//                                                        " forced-wanting" while the supply brain's latch is set
//                                     count <Template>   F2: stderr "[AoC count] frame <frame> logic <frame run>
//                                                        <Template> <n>", the live objects of that template
//                                     place <x> <y>      F8: while a building placement is pending (press on a build
//                                                        button), what a left click on world point x, y sends: the
//                                                        click's checks, then MSG_DOZER_CONSTRUCT with the template, the
//                                                        point on the ground and the placement angle; a refused spot is
//                                                        reported and keeps placement mode; line builds are refused; it
//                                                        skips the click's special-power branch (GLA sneak attack)
//                                     queue              F11: stderr "[AoC queue] frame <frame> logic <frame run> <objectId>
//                                                        <template> entries=<n>: <type> <name> <percent>% <frames>/<total>
//                                                        id <productionId>; ..." with the production queue of the first
//                                                        selected object (lowest id); type unit, upgrade or trade
//                                     cancel <n>         F11: GBM_SELECTED to the visible, enabled production queue button n
//                                                        (1 to 9, ControlBar.wnd:ButtonQueue0n), as a click: cancels that
//                                                        entry (a unit or a shipment through MSG_CANCEL_UNIT_CREATE)
//                                   F11: the log line ends " chip=\"<text>\"" while the airdrop chip (ControlBar.wnd:AoCTimerChip)
//                                   is shown. With any harness flag and AoCTrade blocks defined, the logic prints "[AoC trade] frame
//                                   F player N queued|delivered|cancelled <name> cost|payout|refund <vector> id <productionId>
//                                   object <id> bank <Type>=<n>...", "... refused <name> <reason>" (not-offered, requires, funds,
//                                   queue-full; " (click, nothing sent)" when the client refused it) and the timed drop module
//                                   "[AoC drop] frame F player N armed <upgrade> object <id>" and "... drop <ocl> object <id> next
//                                   <frame> bank <Type>=<n>...". -aiLog adds trades_delivered while AoCTrade blocks are defined.
//                                   After select, selectall, press, back, dock, place and cancel the next line waits 3 engine
//                                   iterations so the control bar re-evaluates. Use absolute paths. select, selectall,
//                                   dock and place send the stock message a click sends, so they move the CRC and the
//                                   replay records them; press, back, log, bank, count and screenshot send nothing. Each of
//                                   select, selectall, dock and place must be the first line of its frame and come after the
//                                   lines before it have settled: a late one (warned on stderr) lands on a frame
//                                   that depends on the render rate, and the CRC log is no longer reproducible.
//                                   Refused under -headless (no windows). Over 64 lines or a line over 511
//                                   characters is reported on stderr and skipped.
//
//   -aocUserData <dir>              F12a: the user-data folder of this process (created if missing) instead of the one
//                                   the registry names (Documents\Command and Conquer Generals Zero Hour Data), so a
//                                   run's saves, replays (Replays\00000000.rep), preferences (Options.ini, Skirmish.ini,
//                                   Network.ini, SagePatch.ini), map cache (Maps\MapCache.ini), screenshots, crash files
//                                   and Debug dumps land in a folder of its own and several games can run at once.
//                                   Parsed in a pass of its own before anything reads the folder (Options.ini's
//                                   SimulationRate included); a relative path is taken against the working directory
//                                   before -setCwd changes it (AoCUserData.h has the rules). -loadsave <name> and
//                                   -loadreplay <name> resolve inside it, and so do user maps: copy the maps a run
//                                   needs into <dir>\Maps first. Exit code 6 when the folder is unusable: no or an
//                                   unsafe name, longer than 180 characters once resolved (the engine's 260-byte path
//                                   buffers, AoCUserData::MAX_FOLDER_LENGTH), not creatable, a file, or not writable.
//                                   Not for LAN tests with several clients on one machine: every folder's game is its
//                                   folder's first instance, so RTS_MULTI_INSTANCE builds give them all one address.
//   (-headless)                     F12a: a -headless process with any harness flag runs unthrottled: the frame pacer
//                                   does not wait for the render cap and counts each engine iteration as one render step
//                                   of it (1/60 s), so the engine sees a game rendering at exactly the cap and runs as
//                                   fast as the CPU allows. Only wall-clock pacing changes: the logic counts 30 or 60
//                                   frames per game second, the client's time scale is a windowed game's, and the CRC
//                                   log equals a windowed run's.
//
// Exit codes: 0 normal, 2 CRC mismatch seen, 3 unsupported -simrate, 4 -skirmish could not start,
// 5 a replay or save made at the other simulation rate was refused, 6 the -aocUserData folder is unusable.
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
	static Int parseAocUserData( char *args[], int num );	// AoC F12a: in a pass of its own, before Options.ini is read
	static Int parseCrcDump( char *args[], int num );	// AoC F14a

	/// TRUE when any harness flag was given on the command line.
	static Bool isActive();

	/// F12a: TRUE in a -headless process with any harness flag. FramePacer::update then neither waits for the render cap nor
	/// measures the wall clock: each engine iteration counts as one render step of the cap (unthrottledStepSeconds), so the
	/// logic time step, the client's time scale and the logic frame count are those of a game rendering at exactly the cap,
	/// and the game runs as fast as the CPU allows.
	static Bool isUnthrottled();

	/// F12a: the duration FramePacer::update reports for one engine iteration of an unthrottled run: one render step of the
	/// frame limit in effect (1/60 s at the stock cap), or one logic frame when the limit is off or unset.
	static Real unthrottledStepSeconds( UnsignedInt maxFps );

	/// F12a: the folder -aocUserData installed, with its trailing separator; nullptr without the flag.
	static const char *userDataOverride();

	/// F12a: rts::ClientInstance::initialize, the instance mutex name: the stock name without -aocUserData, else the name
	/// of the first instance of that folder (AoCUserData::instanceName), so one game runs per user-data folder.
	static const char *instanceMutexName( const char *firstInstanceName );

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

	/// ProductionUpdate delivered a shipment (D11 F11): counted per player for the -aiLog trades_delivered column (written while
	/// AoCTrade blocks are defined). Harness state only, never saved: after a load the count starts again at 0.
	static void onTradeDelivered( const Player *player );
};
