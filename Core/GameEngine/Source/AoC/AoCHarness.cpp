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

// AoCHarness.cpp
// Age of Commanders: unattended-testing harness (design D11 F12, D9 section 7). See AoCHarness.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCHarness.h"
#include "AoC/AoCPopulation.h"
#include "AoC/AoCResourceBank.h"
#include "AoC/AoCRules.h"
#include "AoC/AoCSimRate.h"

#include "Common/BuildAssistant.h"
#include "Common/Energy.h"
#include "Common/FileSystem.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/KindOf.h"
#include "Common/MessageStream.h"
#include "Common/Money.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"
#include "Common/ScoreKeeper.h"
#include "Common/ThingTemplate.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameNetwork/GameInfo.h"

#include <algorithm>
#include <chrono>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

namespace
{

// Seconds of game time between two AI log rows (D9 specifies its logging intervals in seconds):
// 900 frames at 30 Hz, 1800 at 60 Hz, so rows of the two rates line up by game time.
const UnsignedInt AOC_AI_LOG_INTERVAL_SECONDS = 30;
inline UnsignedInt aiLogIntervalFrames() { return AOC_AI_LOG_INTERVAL_SECONDS * (UnsignedInt)LOGICFRAMES_PER_SECOND; }

// F4: CanMakeType values the blocked_build column can name (CANMAKE_OK .. CANMAKE_NO_HOUSING, room to grow).
const Int AOC_HARNESS_CANMAKE_SLOTS = 16;

// Fixed-size storage only: the options are set before the game memory manager owns the heap and are
// read until after it has shut down, so nothing here may allocate.
struct HarnessState
{
	Bool anyFlag;

	Bool hasSkirmish;
	char skirmishMap[1024];
	char skirmishSlots[1024];
	Int skirmishSeed;

	UnsignedInt quitAtFrame;
	Bool quitRequested;		///< frame N was reached inside the logic update
	Bool quitDone;				///< the engine loop has acted on it
	Bool sawGame;					///< a real (non-shell) game has been running at least once

	char crcLogPath[1024];
	FILE *crcLog;

	char aiLogDir[1024];
	Bool aiLogDirCreated;

	UnsignedInt saveAtFrame;	///< F1: 0 when unused
	char saveName[256];
	Bool saveRequested;
	Bool saveDone;

	// F4: unit queue refusals per player index and CanMakeType since the last -aiLog row
	UnsignedInt queueRefusals[MAX_PLAYER_COUNT][AOC_HARNESS_CANMAKE_SLOTS];
	Bool sawFirstLogicFrame;

	Bool sawCRCMismatch;
	UnsignedInt mismatchFrame;

	Bool warnedCrcLog;
	Bool warnedAiLog;

	int exitCodeOverride;	///< -1 when unused

	// Logic frame timing for -aiLog/timing.csv (D11 F15 test e). Nanoseconds from std::chrono::steady_clock.
	long long logicUpdateBeginNs;
	long long logicNs;				///< sum of logic update durations in the current window
	long long logicMaxNs;			///< longest logic update in the current window
	UnsignedInt logicUpdates;	///< logic updates in the current window
	UnsignedInt iterations;		///< engine loop iterations (rendered frames) in the current window
	long long windowStartNs;
};

long long nowNs()
{
	return (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

HarnessState &state()
{
	static HarnessState s = { 0 };
	static Bool initialized = FALSE;
	if (!initialized)
	{
		initialized = TRUE;
		s.exitCodeOverride = -1;
	}
	return s;
}

void copyArg( char *dst, size_t dstSize, const char *src )
{
	strncpy(dst, src, dstSize - 1);
	dst[dstSize - 1] = '\0';
}

void logLine( const char *format, ... )
{
	va_list args;
	va_start(args, format);
	fprintf(stderr, "[AoC] ");
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
	fflush(stderr);
	va_end(args);
}

std::string fmt( const char *format, ... )
{
	char buf[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	buf[sizeof(buf) - 1] = '\0';
	return std::string(buf);
}

Bool inRealGame()
{
	return TheGameLogic && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame();
}

void closeLogs()
{
	HarnessState &s = state();
	if (s.crcLog)
	{
		fclose(s.crcLog);
		s.crcLog = nullptr;
	}
}

void failSkirmish( const char *why )
{
	HarnessState &s = state();
	logLine("-skirmish could not start: %s", why);
	s.exitCodeOverride = 4;
	if (TheGameEngine)
		TheGameEngine->setQuitting(TRUE);
}

// Bring the user's slot list to the exact shape ParseAsciiStringToGameInfo wants: MAX_SLOTS entries,
// ':'-separated, each one terminated by ':'. Missing entries are closed slots.
Bool normalizeSlotList( const char *raw, std::string &out )
{
	std::vector<std::string> entries;
	std::string current;
	for (const char *p = raw; ; ++p)
	{
		if (*p == ':' || *p == '\0')
		{
			if (!current.empty())
				entries.push_back(current);
			current.clear();
			if (*p == '\0')
				break;
		}
		else
		{
			current += *p;
		}
	}
	if (entries.size() > MAX_SLOTS)
		return FALSE;
	while (entries.size() < MAX_SLOTS)
		entries.push_back("X");
	out.clear();
	for (size_t i = 0; i < entries.size(); ++i)
	{
		out += entries[i];
		out += ':';
	}
	return TRUE;
}

// --- AI census ----------------------------------------------------------------------------------

struct Census
{
	Int structures;
	Int structuresBuilding;
	Int units;
	Int inProduction;
	std::vector< std::pair<UnsignedInt, std::string> > completedStructures;	///< object id, template name
};

void censusCallback( Object *obj, void *userData )
{
	Census *c = (Census *)userData;
	if (obj == nullptr || obj->isEffectivelyDead())
		return;

	ProductionUpdateInterface *production = obj->getProductionUpdateInterface();
	if (production)
		c->inProduction += (Int)production->getProductionCount();

	if (obj->isKindOf(KINDOF_STRUCTURE))
	{
		if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
		{
			c->structuresBuilding++;
		}
		else
		{
			c->structures++;
			const ThingTemplate *tmpl = obj->getTemplate();
			c->completedStructures.push_back(std::make_pair((UnsignedInt)obj->getID(), std::string(tmpl ? tmpl->getName().str() : "")));
		}
	}
	else if (obj->isKindOf(KINDOF_INFANTRY) || obj->isKindOf(KINDOF_VEHICLE) || obj->isKindOf(KINDOF_AIRCRAFT))
	{
		c->units++;
	}
}

bool newerObjectFirst( const std::pair<UnsignedInt, std::string> &a, const std::pair<UnsignedInt, std::string> &b )
{
	return a.first > b.first;
}

// F4: the CanMakeType names of the blocked_build column
const char *canMakeName( Int canMakeType )
{
	switch (canMakeType)
	{
		case CANMAKE_OK: return "OK";
		case CANMAKE_NO_PREREQ: return "NO_PREREQ";
		case CANMAKE_NO_MONEY: return "NO_MONEY";
		case CANMAKE_FACTORY_IS_DISABLED: return "FACTORY_IS_DISABLED";
		case CANMAKE_QUEUE_FULL: return "QUEUE_FULL";
		case CANMAKE_PARKING_PLACES_FULL: return "PARKING_PLACES_FULL";
		case CANMAKE_MAXED_OUT_FOR_PLAYER: return "MAXED_OUT_FOR_PLAYER";
		case CANMAKE_NO_HOUSING: return "NO_HOUSING";
	}
	return "OTHER";
}

// F4: "NO_HOUSING:12|NO_MONEY:1" for the refusals counted since the previous row, then reset
std::string takeBlockedBuild( Int playerIndex )
{
	HarnessState &s = state();
	std::string out;
	if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
		return out;
	for (Int r = 0; r < AOC_HARNESS_CANMAKE_SLOTS; ++r)
	{
		UnsignedInt &n = s.queueRefusals[playerIndex][r];
		if (n == 0)
			continue;
		if (!out.empty())
			out += '|';
		out += fmt("%s:%u", canMakeName(r), n);
		n = 0;
	}
	return out;
}

// One CSV row and its header built from the same list, so a new field is one add() line.
// D9 section 7.2 fields (bank per type, workers by type and idle, builders busy, population, houses,
// fields, converters, army value, first-of frames, blocked-build reasons) slot in here as F1, F2 and
// F4 land; keep new columns at the end so existing readers keep working.
struct CsvRow
{
	std::string header;
	std::string values;

	void add( const char *name, const std::string &value )
	{
		if (!header.empty())
		{
			header += ',';
			values += ',';
		}
		header += name;
		values += value;
	}
	void add( const char *name, Int value ) { add(name, fmt("%d", value)); }
	void add( const char *name, UnsignedInt value ) { add(name, fmt("%u", value)); }
};

const char *difficultyName( GameDifficulty d )
{
	switch (d)
	{
		case DIFFICULTY_EASY: return "Easy";
		case DIFFICULTY_NORMAL: return "Medium";
		case DIFFICULTY_HARD: return "Hard";
		default: return "Unknown";
	}
}

std::string csvSafe( const std::string &in )
{
	std::string out = in;
	for (size_t i = 0; i < out.size(); ++i)
		if (out[i] == ',' || out[i] == '\n' || out[i] == '\r')
			out[i] = ' ';
	return out;
}

Bool isLoggedAIPlayer( Player *p )
{
	if (p == nullptr || p->getPlayerType() != PLAYER_COMPUTER)
		return FALSE;
	// Neutral and civilian players are computer players without a starting building.
	const PlayerTemplate *pt = p->getPlayerTemplate();
	return pt != nullptr && !pt->getStartingBuilding().isEmpty();
}

void writeAiLog( UnsignedInt frame )
{
	HarnessState &s = state();
	if (!s.aiLogDirCreated)
	{
		s.aiLogDirCreated = TRUE;
		if (TheFileSystem)
			TheFileSystem->createDirectory(AsciiString(s.aiLogDir));
	}

	std::string dir = s.aiLogDir;
	if (!dir.empty() && dir[dir.size() - 1] != '\\' && dir[dir.size() - 1] != '/')
		dir += '/';

	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *p = ThePlayerList->getNthPlayer(i);
		if (!isLoggedAIPlayer(p))
			continue;

		Census census;
		census.structures = 0;
		census.structuresBuilding = 0;
		census.units = 0;
		census.inProduction = 0;
		p->iterateObjects(censusCallback, &census);
		std::sort(census.completedStructures.begin(), census.completedStructures.end(), newerObjectFirst);
		std::string lastStructures;
		for (size_t n = 0; n < census.completedStructures.size() && n < 3; ++n)
		{
			if (!lastStructures.empty())
				lastStructures += '|';
			lastStructures += census.completedStructures[n].second;
		}

		AsciiString displayName;
		displayName.translate(p->getPlayerDisplayName());

		CsvRow row;
		row.add("frame", frame);
		row.add("player", (Int)p->getPlayerIndex());
		row.add("name", csvSafe(displayName.str()));
		row.add("side", csvSafe(p->getSide().str()));
		row.add("difficulty", std::string(difficultyName(p->getPlayerDifficulty())));
		row.add("money", p->getMoney()->countMoney());
		row.add("money_earned", p->getScoreKeeper()->getTotalMoneyEarned());
		row.add("money_spent", p->getScoreKeeper()->getTotalMoneySpent());
		row.add("power_produced", p->getEnergy()->getProduction());
		row.add("power_consumed", p->getEnergy()->getConsumption());
		row.add("structures", census.structures);
		row.add("structures_building", census.structuresBuilding);
		row.add("units", census.units);
		row.add("in_production", census.inProduction);
		row.add("units_built", p->getScoreKeeper()->getTotalUnitsBuilt());
		row.add("buildings_built", p->getScoreKeeper()->getTotalBuildingsBuilt());
		row.add("last_structures", lastStructures);
		// AoC F1: bank columns, one per defined resource type plus the cash equivalent; absent while the feature is off
		if (AoCRules::enabled())
		{
			for (Int t = 0; t < TheAoCRules->getTypeCount(); ++t)
			{
				std::string column = "bank_" + std::string(TheAoCRules->getTypeName(t).str());
				row.add(column.c_str(), p->getAoCBank()->count(t));
			}
			row.add("cash_equivalent", p->getAoCBank()->cashEquivalent());
			// AoC F4: population, then the unit queue refusals since the previous row
			const AoCPopulation *pop = p->getAoCPopulation();
			row.add("pop_used", pop->getUsed());
			row.add("pop_capacity", pop->getCapacity());
			row.add("pop_reserved", pop->getReserved());
			row.add("pop_cap", pop->getCap());
			row.add("blocked_build", takeBlockedBuild((Int)p->getPlayerIndex()));
		}

		std::string path = dir + fmt("ai_%d.csv", (int)p->getPlayerIndex());
		FILE *f = fopen(path.c_str(), "a");
		if (f == nullptr)
		{
			if (!s.warnedAiLog)
			{
				s.warnedAiLog = TRUE;
				logLine("-aiLog: cannot open '%s' (relative paths resolve against the working directory; only the last folder is created)", path.c_str());
			}
			continue;
		}
		// In append mode the position reads 0 until the first write; seek to learn whether the file is new.
		fseek(f, 0, SEEK_END);
		if (ftell(f) == 0)
			fprintf(f, "%s\n", row.header.c_str());
		fprintf(f, "%s\n", row.values.c_str());
		fclose(f);
	}
}

// F4: one stderr line per AI player that takes part in the population rules (save and load checks)
void logPopulation( const char *when, UnsignedInt frame )
{
	if (ThePlayerList == nullptr)
		return;
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *p = ThePlayerList->getNthPlayer(i);
		if (!isLoggedAIPlayer(p) || !p->getAoCPopulation()->isActive())
			continue;
		const AoCPopulation *pop = p->getAoCPopulation();
		logLine("population %s, frame %u: player %d used %d capacity %d reserved %d cap %d headroom %d", when, frame,
			(int)p->getPlayerIndex(), pop->getUsed(), pop->getCapacity(), pop->getReserved(), pop->getCap(), pop->getHeadroom());
	}
}

void writeTimingLog( UnsignedInt frame )
{
	HarnessState &s = state();
	std::string dir = s.aiLogDir;
	if (!dir.empty() && dir[dir.size() - 1] != '\\' && dir[dir.size() - 1] != '/')
		dir += '/';
	const long long now = nowNs();
	const double wallMs = s.windowStartNs ? (double)(now - s.windowStartNs) / 1.0e6 : 0.0;
	const double logicMs = (double)s.logicNs / 1.0e6;

	CsvRow row;
	row.add("frame", frame);
	row.add("rate_hz", LOGICFRAMES_PER_SECOND);
	row.add("wall_ms", fmt("%.0f", wallMs));
	row.add("engine_iterations", s.iterations);
	row.add("render_fps", fmt("%.1f", wallMs > 0.0 ? (double)s.iterations * 1000.0 / wallMs : 0.0));
	row.add("logic_updates", s.logicUpdates);
	row.add("logic_avg_ms", fmt("%.3f", s.logicUpdates ? logicMs / (double)s.logicUpdates : 0.0));
	row.add("logic_max_ms", fmt("%.3f", (double)s.logicMaxNs / 1.0e6));
	row.add("logic_load_pct", fmt("%.1f", wallMs > 0.0 ? logicMs * 100.0 / wallMs : 0.0));

	std::string path = dir + "timing.csv";
	FILE *f = fopen(path.c_str(), "a");
	if (f)
	{
		fseek(f, 0, SEEK_END);
		if (ftell(f) == 0)
			fprintf(f, "%s\n", row.header.c_str());
		fprintf(f, "%s\n", row.values.c_str());
		fclose(f);
	}

	s.windowStartNs = now;
	s.logicNs = 0;
	s.logicMaxNs = 0;
	s.logicUpdates = 0;
	s.iterations = 0;
}

} // namespace

// --- command line -------------------------------------------------------------------------------

Int AoCHarness::parseSkirmish( char *args[], int num )
{
	HarnessState &s = state();
	if (num < 4)
	{
		logLine("usage: -skirmish <map> <slots> <seed>");
		return 1;
	}
	copyArg(s.skirmishMap, sizeof(s.skirmishMap), args[1]);
	copyArg(s.skirmishSlots, sizeof(s.skirmishSlots), args[2]);
	s.skirmishSeed = atoi(args[3]);
	s.hasSkirmish = TRUE;
	s.anyFlag = TRUE;

	// Same start-up shortcuts as -loadreplay.
	TheWritableGlobalData->m_shellMapOn = FALSE;
	TheWritableGlobalData->m_playIntro = FALSE;
	TheWritableGlobalData->m_playSizzle = FALSE;
	return 4;
}

Int AoCHarness::parseQuitAtFrame( char *args[], int num )
{
	if (num > 1)
	{
		HarnessState &s = state();
		s.quitAtFrame = (UnsignedInt)strtoul(args[1], nullptr, 10);
		s.anyFlag = TRUE;
		return 2;
	}
	return 1;
}

Int AoCHarness::parseCrcLog( char *args[], int num )
{
	if (num > 1)
	{
		HarnessState &s = state();
		copyArg(s.crcLogPath, sizeof(s.crcLogPath), args[1]);
		s.anyFlag = TRUE;
		return 2;
	}
	return 1;
}

Int AoCHarness::parseAiLog( char *args[], int num )
{
	if (num > 1)
	{
		HarnessState &s = state();
		copyArg(s.aiLogDir, sizeof(s.aiLogDir), args[1]);
		s.anyFlag = TRUE;
		return 2;
	}
	return 1;
}

Int AoCHarness::parseSaveAtFrame( char *args[], int num )
{
	if (num < 3)
	{
		logLine("usage: -saveAtFrame <N> <name> (N at least 1); flag ignored");
		return 1;
	}
	const UnsignedInt frame = (UnsignedInt)strtoul(args[1], nullptr, 10);
	if (frame == 0 || args[2][0] == '\0')
	{
		// 0 is the "unused" value of saveAtFrame: say so instead of dropping the flag silently.
		logLine("-saveAtFrame needs a frame of at least 1 and a name, not '%s' '%s'; no save will be written", args[1], args[2]);
		return 3;
	}
	HarnessState &s = state();
	s.saveAtFrame = frame;
	copyArg(s.saveName, sizeof(s.saveName), args[2]);
	s.anyFlag = TRUE;
	return 3;
}

Int AoCHarness::parseSimRate( char *args[], int num )
{
	if (num > 1)
	{
		const Int rate = atoi(args[1]);
		if (AoCSimRate::set(rate, "-simrate"))
			return 2;
		logLine("-simrate accepts 30 or 60, not '%s'", args[1]);
		exit(3);
	}
	return 1;
}

Bool AoCHarness::isActive()
{
	return state().anyFlag;
}

// --- skirmish start -----------------------------------------------------------------------------

void AoCHarness::startSkirmishIfRequested()
{
	HarnessState &s = state();
	if (!s.hasSkirmish)
		return;
	s.hasSkirmish = FALSE;

	if (TheMapCache == nullptr || TheSkirmishGameInfo != nullptr && TheSkirmishGameInfo->isInGame())
	{
		failSkirmish("engine not ready");
		return;
	}

	const MapMetaData *md = TheMapCache->findMap(AsciiString(s.skirmishMap));
	if (md == nullptr)
	{
		failSkirmish(fmt("map '%s' is not in the map cache (user maps live in '%s')", s.skirmishMap, TheMapCache->getUserMapDir().str()).c_str());
		return;
	}
	const AsciiString mapName = md->m_fileName;

	std::string slotList;
	if (!normalizeSlotList(s.skirmishSlots, slotList))
	{
		failSkirmish("more than 8 slot entries");
		return;
	}

	// Same sequence as SkirmishGameOptionsMenuInit.
	if (TheSkirmishGameInfo == nullptr)
		TheSkirmishGameInfo = NEW SkirmishGameInfo;
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->setLocalIP(0);
	TheSkirmishGameInfo->enterGame();

	// The options string is produced by the stock writer (header part) and parsed by the stock parser,
	// so the slot grammar is exactly the replay one. Only the slot list after "S=" is ours.
	SkirmishGameInfo scratch;
	scratch.init();
	scratch.setMap(mapName);
	scratch.setMapCRC(md->m_CRC);
	scratch.setMapSize(md->m_filesize);
	scratch.setMapContentsMask(1);
	scratch.setSeed(s.skirmishSeed);
	scratch.setCRCInterval(100);
	const AsciiString header = GameInfoToAsciiString(&scratch);
	const char *slotKey = strstr(header.str(), ";S=");
	if (slotKey == nullptr)
	{
		failSkirmish("could not build the game options string");
		return;
	}
	std::string options(header.str(), (size_t)(slotKey - header.str()) + 3);
	options += slotList;
	options += ';';

	if (!ParseAsciiStringToGameInfo(TheSkirmishGameInfo, AsciiString(options.c_str())))
	{
		failSkirmish(fmt("slot list rejected by ParseAsciiStringToGameInfo: '%s'", slotList.c_str()).c_str());
		return;
	}

	GameSlot *local = TheSkirmishGameInfo->getSlot(0);
	if (local == nullptr || !local->isHuman())
	{
		failSkirmish("slot 0 must be the human local player, for example HAoC,0,0,TT,-1,0,0,-1,0");
		return;
	}
	TheSkirmishGameInfo->setLocalIP(local->getIP());
	TheSkirmishGameInfo->setMap(mapName);	// again as the host, to compute the map contents mask
	TheSkirmishGameInfo->setSeed(s.skirmishSeed);

	if (TheSkirmishGameInfo->getNumPlayers() > md->m_numPlayers)
	{
		failSkirmish(fmt("%d players but the map has %d start positions", TheSkirmishGameInfo->getNumPlayers(), md->m_numPlayers).c_str());
		return;
	}

	logLine("skirmish map '%s' (%d start positions, multiplayer=%d) seed %d, %d players",
		mapName.str(), md->m_numPlayers, md->m_isMultiplayer ? 1 : 0, s.skirmishSeed, TheSkirmishGameInfo->getNumPlayers());
	logLine("skirmish options: %s", GameInfoToAsciiString(TheSkirmishGameInfo).str());
	for (Int i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate *pt = ThePlayerTemplateStore->getNthPlayerTemplate(i);
		if (pt && !pt->getStartingBuilding().isEmpty())
			logLine("player template %d = %s (side %s)", i, pt->getName().str(), pt->getSide().str());
	}

	// Same as SkirmishGameOptionsMenu reallyDoStart(), without the game speed slider.
	TheWritableGlobalData->m_mapName = mapName;
	TheSkirmishGameInfo->startGame(0);
	InitRandom(s.skirmishSeed);

	GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_NEW_GAME);
	msg->appendIntegerArgument(md->m_isMultiplayer ? GAME_SKIRMISH : GAME_SINGLE_PLAYER);
	msg->appendIntegerArgument(DIFFICULTY_NORMAL);
	msg->appendIntegerArgument(0);
}

// --- per-frame hooks ----------------------------------------------------------------------------

void AoCHarness::onLogicFrame( UnsignedInt frame, Bool crcGenerated, UnsignedInt crc )
{
	HarnessState &s = state();
	if (!s.anyFlag || !inRealGame())
		return;

	if (crcGenerated && s.crcLogPath[0] != '\0')
	{
		if (s.crcLog == nullptr)
		{
			s.crcLog = fopen(s.crcLogPath, "a");
			if (s.crcLog == nullptr && !s.warnedCrcLog)
			{
				s.warnedCrcLog = TRUE;
				logLine("-crcLog: cannot open '%s' (relative paths resolve against the working directory; the folder must exist)", s.crcLogPath);
			}
		}
		if (s.crcLog)
		{
			fprintf(s.crcLog, "%u,%08X\n", frame, crc);
			fflush(s.crcLog);
		}
	}

	// F4: a game whose first logic frame is past 1 was loaded from a save
	if (!s.sawFirstLogicFrame)
	{
		s.sawFirstLogicFrame = TRUE;
		if (frame > 1 && s.aiLogDir[0] != '\0')
			logPopulation("after load", frame);
	}

	if (s.aiLogDir[0] != '\0' && (frame % aiLogIntervalFrames()) == 0)
	{
		writeAiLog(frame);
		writeTimingLog(frame);
	}

	if (s.saveAtFrame != 0 && frame >= s.saveAtFrame && !s.saveRequested)
	{
		s.saveRequested = TRUE;	// the save itself happens in pollQuit, outside the logic update
		logLine("saveAtFrame %u reached", frame);
	}

	if (s.quitAtFrame != 0 && frame >= s.quitAtFrame && !s.quitRequested)
	{
		s.quitRequested = TRUE;
		logLine("quitAtFrame %u reached", frame);
		if (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_SIMULATION_PLAYBACK)
		{
			// Headless replay simulation runs its own loop until playback stops. Stopping it here is what
			// the recorder itself does from inside the logic update when the replay file ends.
			closeLogs();
			TheRecorder->stopPlayback();
		}
		// Windowed games finish in pollQuit(), from the engine loop.
	}
}

void AoCHarness::onCRCMismatch( UnsignedInt frame, UnsignedInt inGameCRC, UnsignedInt replayCRC )
{
	HarnessState &s = state();
	if (!s.sawCRCMismatch)
	{
		s.sawCRCMismatch = TRUE;
		s.mismatchFrame = frame;
	}
	if (s.anyFlag)
		logLine("replay CRC mismatch at frame %u: in game %08X, replay %08X", frame, inGameCRC, replayCRC);
}

void AoCHarness::logicUpdateBegin()
{
	HarnessState &s = state();
	if (s.anyFlag)
		s.logicUpdateBeginNs = nowNs();
}

void AoCHarness::logicUpdateEnd()
{
	HarnessState &s = state();
	if (!s.anyFlag || s.logicUpdateBeginNs == 0)
		return;
	const long long d = nowNs() - s.logicUpdateBeginNs;
	s.logicUpdateBeginNs = 0;
	s.logicNs += d;
	if (d > s.logicMaxNs)
		s.logicMaxNs = d;
	s.logicUpdates++;
}

void AoCHarness::onUnitQueueRefused( const Player *player, Int canMakeType )
{
	HarnessState &s = state();
	if (!s.anyFlag || player == nullptr)
		return;
	const Int index = (Int)player->getPlayerIndex();
	if (index < 0 || index >= MAX_PLAYER_COUNT || canMakeType < 0 || canMakeType >= AOC_HARNESS_CANMAKE_SLOTS)
		return;
	s.queueRefusals[index][canMakeType]++;
}

void AoCHarness::onSimRateRefusal()
{
	HarnessState &s = state();
	if (!s.anyFlag)
		return;	// a player at the menus keeps the message box and the shell
	s.exitCodeOverride = 5;
	logLine("leaving: a replay or save at the other simulation rate was refused (exit code 5)");
	if (TheGameEngine)
		TheGameEngine->setQuitting(TRUE);
}

void AoCHarness::pollQuit()
{
	HarnessState &s = state();
	if (s.anyFlag)
	{
		s.iterations++;
		if (s.windowStartNs == 0)
			s.windowStartNs = nowNs();
	}

	// F1: -saveAtFrame, through the save menu's path, between two logic frames like the menu does.
	if (s.saveRequested && !s.saveDone && inRealGame() && TheGameState)
	{
		s.saveDone = TRUE;
		AsciiString filename = s.saveName;
		if (!filename.endsWithNoCase(".sav"))
			filename.concat(".sav");
		if (s.aiLogDir[0] != '\0')
			logPopulation("at save", TheGameLogic->getFrame());	// F4
		UnicodeString desc;
		desc.format(L"AoC harness save at frame %u", TheGameLogic->getFrame());
		SaveResult result = TheGameState->saveGame(filename, desc, SAVE_FILE_TYPE_NORMAL);
		logLine("saveAtFrame: saveGame('%s') at logic frame %u returned %d", filename.str(), TheGameLogic->getFrame(), (int)result.saveCode);
	}

	if (s.quitAtFrame == 0 || s.quitDone)
		return;

	if (inRealGame())
	{
		s.sawGame = TRUE;
		// Stock pauses a replay on its first CRC mismatch. The harness keeps it running so the run still
		// ends at frame N and reports the mismatch through the exit code.
		if (s.sawCRCMismatch && TheGameLogic->isGamePaused())
			TheGameLogic->setGamePaused(FALSE, FALSE, FALSE);
		if (!s.quitRequested)
			return;
	}
	else
	{
		// Not in a game: either it has not started yet, or it ended before frame N (a replay ran out).
		if (!s.sawGame || TheGameLogic->isLoadingMap())
			return;
	}

	s.quitDone = TRUE;
	const UnsignedInt frame = TheGameLogic->getFrame();
	closeLogs();
	// Same steps as GameLogic::quit(TRUE) without the quit menu.
	if (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
		TheRecorder->stopRecording();
	if (TheGameLogic->isInGame())
	{
		TheGameLogic->setGamePaused(FALSE);
		TheGameLogic->clearGameData(FALSE);
	}
	TheGameEngine->setQuitting(TRUE);
	logLine("leaving at logic frame %u, crc mismatch seen: %d", frame, s.sawCRCMismatch ? 1 : 0);
}

int AoCHarness::finalExitCode( int exitcode )
{
	HarnessState &s = state();
	if (s.exitCodeOverride >= 0)
		return s.exitCodeOverride;
	if (!s.anyFlag)
		return exitcode;
	closeLogs();
	if (s.sawCRCMismatch)
		return 2;
	return exitcode;
}
