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

#include "Common/ActionManager.h"
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
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameNetwork/GameInfo.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
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
// F4: distinct templates the blocked_units column remembers per player and row.
const Int AOC_HARNESS_BLOCKED_UNITS = 8;

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
	const ThingTemplate *housingRefused[MAX_PLAYER_COUNT][AOC_HARNESS_BLOCKED_UNITS];
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

	// F5 -aocUiScript: client UI steps, each run once its logic frame has run (see runUiScript).
	struct UiLine { UnsignedInt frame; char cmd[12]; char arg[244]; };
	UiLine uiLines[64];
	Int uiLineCount;
	Int uiNext;									///< next line to run
	Int uiWait;									///< engine iterations left before the next line, so the UI catches up
	Bool sawLogicFrame;
	UnsignedInt lastLogicFrame;	///< the last logic frame that has run
	char uiShotTarget[1024];		///< pending screenshot: where the engine's PNG goes
	char uiShotBefore[64];			///< the newest engine screenshot name before the request
	Int uiShotWait;							///< iterations left to find it, 0 when none is pending
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

#if RTS_ZEROHOUR
// AoC F2: D9's gatherer census, through the typed gathering interface (Zero Hour only; base Generals has no typed cargo).
struct GathererCensus
{
	Int byAssignedType[AOC_MAX_RESOURCES];	///< gatherers whose assigned type is that type
	Int idle;																///< gatherers neither ferrying nor doing anything else
};

void gathererCensusCallback( Object *obj, void *userData )
{
	GathererCensus *c = (GathererCensus *)userData;
	if (obj == nullptr || obj->isEffectivelyDead() || obj->getAI() == nullptr)
		return;
	const SupplyTruckAIInterface *gatherer = obj->getAI()->getSupplyTruckAIInterface();
	if (gatherer == nullptr)
		return;
	const Int assigned = gatherer->getAssignedResourceType();
	if (assigned >= 0 && assigned < AOC_MAX_RESOURCES)
		c->byAssignedType[assigned]++;
	if (gatherer->isIdleGatherer())
		c->idle++;
}
#endif

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

// F4: the objects that make up a player's population, by template, for the over-the-cap stderr line
void populationByTemplate( Object *obj, void *userData )
{
	std::vector< std::pair<std::string, Int> > *list = (std::vector< std::pair<std::string, Int> > *)userData;
	const Int cost = AoCPopulation::livingCostOf(obj);	// 0 for the dead, whose slot is already free
	if (cost == 0)
		return;
	const std::string name = obj->getTemplate()->getName().str();
	for (size_t i = 0; i < list->size(); ++i)
	{
		if ((*list)[i].first == name)
		{
			(*list)[i].second += cost;
			return;
		}
	}
	list->push_back(std::make_pair(name, cost));
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

// F4: "ChinaVehicleTroopCrawler(9)|ChinaInfantryRedguard(2)" for the templates refused for housing since the previous
// row, with the population each needs (AoCPopulation::productionCostOf, payload included), then reset
std::string takeBlockedUnits( Int playerIndex )
{
	HarnessState &s = state();
	std::string out;
	if (playerIndex < 0 || playerIndex >= MAX_PLAYER_COUNT)
		return out;
	for (Int n = 0; n < AOC_HARNESS_BLOCKED_UNITS; ++n)
	{
		const ThingTemplate *t = s.housingRefused[playerIndex][n];
		if (t == nullptr)
			continue;
		if (!out.empty())
			out += '|';
		out += fmt("%s(%d)", t->getName().str(), (int)AoCPopulation::productionCostOf(t));
		s.housingRefused[playerIndex][n] = nullptr;
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
			row.add("blocked_units", takeBlockedUnits((Int)p->getPlayerIndex()));
			if (pop->getUsed() + pop->getReserved() > pop->getCap())
			{
				std::vector< std::pair<std::string, Int> > parts;
				p->iterateObjects(populationByTemplate, &parts);
				std::sort(parts.begin(), parts.end());
				std::string what;
				for (size_t n = 0; n < parts.size(); ++n)
					what += fmt(" %s:%d", parts[n].first.c_str(), (int)parts[n].second);
				logLine("population over the cap, frame %u, player %d: used %d reserved %d cap %d; by template%s", frame,
					(int)p->getPlayerIndex(), pop->getUsed(), pop->getReserved(), pop->getCap(), what.c_str());
			}
#if RTS_ZEROHOUR
			// AoC F2: the gatherer census (D9), by assigned type plus the idle count
			GathererCensus gatherers;
			memset(&gatherers, 0, sizeof(gatherers));
			p->iterateObjects(gathererCensusCallback, &gatherers);
			for (Int t = 0; t < TheAoCRules->getTypeCount() && t < AOC_MAX_RESOURCES; ++t)
			{
				std::string column = "workers_" + std::string(TheAoCRules->getTypeName(t).str());
				row.add(column.c_str(), gatherers.byAssignedType[t]);
			}
			row.add("workers_idle", gatherers.idle);
#endif
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

// --- F5 -aocUiScript ----------------------------------------------------------------------------
// Client UI steps through the paths a click or a key takes, never logic state. After select, press and back
// the next line waits a few engine iterations: one client update re-evaluates the control bar, and injected
// keys need one more to pass the message stream.

const Int AOC_UI_SETTLE_ITERATIONS = 3;

GameWindow *uiCommandWindow( Int slot )	// slot 1 to 14: the windows ControlBar::init looks up
{
	AsciiString name;
	name.format("ControlBar.wnd:ButtonCommand%02d", slot);
	return TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey(name));
}

Bool uiShown( GameWindow *win )
{
	for (GameWindow *w = win; w; w = w->winGetParent())
		if (w->winIsHidden())
			return FALSE;
	return win != nullptr;
}

std::filesystem::path uiScreenshotDir()	// where W3DScreenshot writes sshot_<local time>.png
{
	return std::filesystem::path(TheGlobalData->getPath_UserData().str()) / "Screenshots";
}

std::string uiNewestScreenshot()
{
	std::string newest;
	std::error_code ec;
	for (std::filesystem::directory_iterator it(uiScreenshotDir(), ec), end; !ec && it != end; it.increment(ec))
	{
		const std::string name = it->path().filename().string();
		if (name.compare(0, 6, "sshot_") == 0 && it->path().extension() == ".png" && name > newest)
			newest = name;
	}
	return newest;
}

void uiLog( UnsignedInt frame )
{
	std::string slots;
	for (Int slot = 1; slot <= 14; ++slot)
	{
		GameWindow *win = uiCommandWindow(slot);
		const CommandButton *button = uiShown(win) ? (const CommandButton *)GadgetButtonGetData(win) : nullptr;
		slots += std::string(slot > 1 ? "," : "") + (button ? button->getName().str() : "-");
	}
	const AsciiString &set = TheControlBar->aocShownCommandSet();
	const ThingTemplate *placing = TheInGameUI->getPendingPlaceType();
	fprintf(stderr, "[AoC ui] frame %u set=%s slots=%s%s%s\n", frame, set.isEmpty() ? "-" : set.str(), slots.c_str(),
		placing ? " place=" : "", placing ? placing->getName().str() : "");
	fflush(stderr);
}

// --- F2 additions: dock, selectall, bank -----------------------------------------------------------

Bool uiIsLocalCandidate( Object *obj, const char *templateName )
{
	return obj->getControllingPlayer() == ThePlayerList->getLocalPlayer() && obj->getDrawable() && !obj->getContainedBy() &&
		!obj->isEffectivelyDead() && obj->getTemplate()->getName().compareNoCase(templateName) == 0;
}

/// The selected object of the local player with the lowest id (the client's selection list), or nullptr.
Object *uiFirstSelected()
{
	Object *first = nullptr;
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	if (selected == nullptr)
		return nullptr;
	for (DrawableListCIt it = selected->begin(); it != selected->end(); ++it)
	{
		Object *obj = (*it) ? (*it)->getObject() : nullptr;
		if (obj && obj->getControllingPlayer() == ThePlayerList->getLocalPlayer() && (!first || obj->getID() < first->getID()))
			first = obj;
	}
	return first;
}

/// selectall <Template>: every object of the local player with that template, selected in one message as a drag box
/// selects them (deselect all, the stock MSG_CREATE_SELECTED_GROUP with every id in id order, select the drawables).
Int uiSelectAll( const HarnessState::UiLine &l )
{
	std::vector<Object *> picks;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
		if (uiIsLocalCandidate(obj, l.arg))
			picks.push_back(obj);
	if (picks.empty())
	{
		logLine("uiScript: selectall %s: the local player has no such object", l.arg);
		return 0;
	}
	std::sort(picks.begin(), picks.end(), [](const Object *a, const Object *b) { return a->getID() < b->getID(); });
	TheInGameUI->deselectAllDrawables();
	GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
	msg->appendBooleanArgument(TRUE);
	std::string ids;
	for (size_t i = 0; i < picks.size(); ++i)
	{
		msg->appendObjectIDArgument(picks[i]->getID());
		TheInGameUI->selectDrawable(picks[i]->getDrawable());
		ids += fmt("%s%u", i ? "," : "", (UnsignedInt)picks[i]->getID());
	}
	logLine("uiScript: selectall %s: %d objects (%s)", l.arg, (int)picks.size(), ids.c_str());
	return AOC_UI_SETTLE_ITERATIONS;
}

/// dock <Template>: the stock dock order of the current selection (MSG_DOCK, what a right-click on a dock sends,
/// CommandTranslator::handleDockAtCommand) on the object of that template nearest the first selected object
/// (2D distance, the lower id on a tie). Through the message stream, so the replay records it.
Int uiDock( const HarnessState::UiLine &l )
{
	Object *first = uiFirstSelected();
	if (first == nullptr)
	{
		logLine("uiScript: dock %s: nothing of the local player is selected", l.arg);
		return 0;
	}
	const Coord3D *from = first->getPosition();
	Object *target = nullptr;
	Real best = 0.0f;
	for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->isEffectivelyDead() || obj->getTemplate()->getName().compareNoCase(l.arg) != 0)
			continue;
		const Real dx = obj->getPosition()->x - from->x;
		const Real dy = obj->getPosition()->y - from->y;
		const Real d = dx * dx + dy * dy;
		if (target == nullptr || d < best || (d == best && obj->getID() < target->getID()))
		{
			target = obj;
			best = d;
		}
	}
	if (target == nullptr)
	{
		logLine("uiScript: dock %s: no such object on the map", l.arg);
		return 0;
	}
	GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_DOCK);
	msg->appendObjectIDArgument(target->getID());
	logLine("uiScript: dock %s: target %u at (%.0f, %.0f), first selected %u, legal for it: %d", l.arg, (UnsignedInt)target->getID(),
		target->getPosition()->x, target->getPosition()->y, (UnsignedInt)first->getID(),
		TheActionManager->canDockAt(first, target, CMD_FROM_PLAYER) ? 1 : 0);
	return AOC_UI_SETTLE_ITERATIONS;
}

/// bank: one stderr line with the local player's bank and the first selected object's cargo. Reads only.
void uiBank( UnsignedInt frame )
{
	HarnessState &s = state();
	Player *local = ThePlayerList->getLocalPlayer();
	std::string text = fmt("[AoC bank] frame %u logic %u", frame, s.lastLogicFrame);
	const Int typeCount = AoCRules::typeCount();
	for (Int t = 0; t < typeCount && t < AOC_MAX_RESOURCES; ++t)
		text += fmt(" %s=%u", TheAoCRules ? TheAoCRules->getTypeName(t).str() : "Cash", local ? local->getAoCBank()->count(t) : 0u);
	Object *first = uiFirstSelected();
	const SupplyTruckAIInterface *gatherer = (first && first->getAI()) ? first->getAI()->getSupplyTruckAIInterface() : nullptr;
	if (gatherer)
	{
#if RTS_ZEROHOUR
		text += fmt(" cargo %u %s:%d/%d", (UnsignedInt)first->getID(), TheAoCRules ? TheAoCRules->getTypeName(gatherer->getCargoType()).str() : "Cash",
			gatherer->getNumberBoxes(), gatherer->getMaxUnitsFor(gatherer->getCargoType()));
#else
		text += fmt(" cargo %u boxes:%d/%d", (UnsignedInt)first->getID(), gatherer->getNumberBoxes(), gatherer->getMaxBoxes());
#endif
	}
	else
		text += first ? fmt(" cargo %u -", (UnsignedInt)first->getID()) : std::string(" cargo -");
	fprintf(stderr, "%s\n", text.c_str());
	fflush(stderr);
}

/// Runs one line; returns the engine iterations to wait before the next.
Int uiRunLine( const HarnessState::UiLine &l )
{
	HarnessState &s = state();
	if (strcmp(l.cmd, "log") == 0)
	{
		uiLog(l.frame);
		return 0;
	}
	if (strcmp(l.cmd, "bank") == 0)
	{
		uiBank(l.frame);
		return 0;
	}
	if (strcmp(l.cmd, "count") == 0)
	{
		// live objects of a template, any owner (F2 evidence: a node before and after it is killed). Reads only.
		Int n = 0;
		for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
			if (!obj->isEffectivelyDead() && obj->getTemplate()->getName().compareNoCase(l.arg) == 0)
				++n;
		fprintf(stderr, "[AoC count] frame %u logic %u %s %d\n", l.frame, s.lastLogicFrame, l.arg, n);
		fflush(stderr);
		return 0;
	}
	if (strcmp(l.cmd, "selectall") == 0)
		return uiSelectAll(l);
	if (strcmp(l.cmd, "dock") == 0)
		return uiDock(l);
	if (strcmp(l.cmd, "select") == 0)
	{
		// the local player's object of that template with the lowest id, selected as a left click selects it
		Object *pick = nullptr;
		for (Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
			if (obj->getControllingPlayer() == ThePlayerList->getLocalPlayer() && obj->getDrawable() && !obj->getContainedBy() &&
					!obj->isEffectivelyDead() && obj->getTemplate()->getName().compareNoCase(l.arg) == 0 && (!pick || obj->getID() < pick->getID()))
				pick = obj;
		if (pick == nullptr)
		{
			logLine("uiScript: select %s: the local player has no such object", l.arg);
			return 0;
		}
		TheInGameUI->deselectAllDrawables();
		GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
		msg->appendBooleanArgument(TRUE);
		msg->appendObjectIDArgument(pick->getID());
		TheInGameUI->selectDrawable(pick->getDrawable());
		return AOC_UI_SETTLE_ITERATIONS;
	}
	if (strcmp(l.cmd, "press") == 0)
	{
		GameWindow *win = uiCommandWindow(atoi(l.arg));
		if (!uiShown(win) || !BitIsSet(win->winGetStatus(), WIN_STATUS_ENABLED))
		{
			logLine("uiScript: press %s: no visible enabled button in that slot, nothing pressed", l.arg);
			return 0;
		}
		// what a hotkey sends (HotKeyManager::executeHotKey) and a left click ends in: GBM_SELECTED to the control bar
		TheWindowManager->winSendSystemMsg(win->winGetParent(), GBM_SELECTED, (WindowMsgData)win, win->winGetWindowId());
		return AOC_UI_SETTLE_ITERATIONS;
	}
	if (strcmp(l.cmd, "back") == 0)
	{
		// Backspace down and up into the message stream, as the keyboard posts them; CommandMap.ini maps the key
		GameMessage *down = TheMessageStream->appendMessage(GameMessage::MSG_RAW_KEY_DOWN);
		down->appendIntegerArgument(KEY_BACKSPACE);
		down->appendIntegerArgument(KEY_STATE_DOWN);
		GameMessage *up = TheMessageStream->appendMessage(GameMessage::MSG_RAW_KEY_UP);
		up->appendIntegerArgument(KEY_BACKSPACE);
		up->appendIntegerArgument(KEY_STATE_UP);
		return AOC_UI_SETTLE_ITERATIONS;
	}
	// screenshot: the engine's own Release path (Ctrl+F12 PNG); its thread names the file, runUiScript moves it
	copyArg(s.uiShotTarget, sizeof(s.uiShotTarget), l.arg);
	copyArg(s.uiShotBefore, sizeof(s.uiShotBefore), uiNewestScreenshot().c_str());
	TheDisplay->takeScreenShot(SCREENSHOT_PNG);
	s.uiShotWait = 600;
	return AOC_UI_SETTLE_ITERATIONS;
}

void runUiScript()
{
	HarnessState &s = state();
	if (s.uiLineCount == 0 || !s.sawLogicFrame || !inRealGame() || TheControlBar == nullptr)
		return;
	if (s.uiShotWait > 0)
	{
		const std::string newest = uiNewestScreenshot();
		std::error_code ec;
		if (!newest.empty() && newest > std::string(s.uiShotBefore))
			std::filesystem::rename(uiScreenshotDir() / newest, s.uiShotTarget, ec);	// fails while the writer still has it open
		if (!newest.empty() && newest > std::string(s.uiShotBefore) && !ec)
		{
			logLine("uiScript: screenshot %s moved to %s", newest.c_str(), s.uiShotTarget);
			s.uiShotWait = 0;
		}
		else if (--s.uiShotWait == 0)
			logLine("uiScript: no new engine screenshot in %s for '%s'", uiScreenshotDir().string().c_str(), s.uiShotTarget);
	}
	if (s.uiWait > 0)
	{
		--s.uiWait;
		return;
	}
	while (s.uiNext < s.uiLineCount && s.uiLines[s.uiNext].frame <= s.lastLogicFrame && s.uiWait == 0)
	{
		const HarnessState::UiLine &l = s.uiLines[s.uiNext++];
		logLine("uiScript line %d at logic frame %u: %s %s", s.uiNext, s.lastLogicFrame, l.cmd, l.arg);
		// select, selectall and dock (F2) send a logic message, so each must be the first line of its frame
		const Bool sendsLogicMessage = strcmp(l.cmd, "select") == 0 || strcmp(l.cmd, "selectall") == 0 || strcmp(l.cmd, "dock") == 0;
		if (sendsLogicMessage && s.lastLogicFrame > l.frame)
			logLine("uiScript: warning: line %d (%s, frame %u) runs late, at logic frame %u: its message lands on a "
				"frame that depends on the render rate, so the CRC log is not reproducible; make it the first line of its frame", s.uiNext, l.cmd, l.frame, s.lastLogicFrame);
		s.uiWait = uiRunLine(l);
	}
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

Int AoCHarness::parseAocUiScript( char *args[], int num )
{
	if (num < 2)
	{
		logLine("usage: -aocUiScript <file>; flag ignored");
		return 1;
	}
	HarnessState &s = state();
	if (TheGlobalData && TheGlobalData->m_headless)
	{
		// the dummy window manager has no command buttons, but a select would still reach the logic
		logLine("-aocUiScript needs a window and is ignored under -headless");
		return 2;
	}
	FILE *f = fopen(args[1], "r");
	if (f == nullptr)
	{
		logLine("-aocUiScript: cannot open '%s'; flag ignored", args[1]);
		return 2;
	}
	static const char *const known[] = { "select", "press", "back", "log", "screenshot", "dock", "selectall", "bank", "count" };	// F2: dock, selectall, bank, count
	char text[512];
	Int lineNumber = 0;
	Int dropped = 0;
	while (fgets(text, sizeof(text), f))
	{
		++lineNumber;
		int c = strchr(text, '\n') == nullptr ? fgetc(f) : '\n';
		if (c != '\n' && c != EOF)
		{
			// longer than the buffer: skip the rest of it rather than read its tail as a line of its own
			while ((c = fgetc(f)) != EOF && c != '\n') {}
			logLine("-aocUiScript: line %d is longer than %d characters, skipped", lineNumber, (int)sizeof(text) - 1);
			continue;
		}
		HarnessState::UiLine scratch;
		HarnessState::UiLine &l = s.uiLineCount < (Int)ARRAY_SIZE(s.uiLines) ? s.uiLines[s.uiLineCount] : scratch;
		l.arg[0] = '\0';
		if (sscanf(text, "%u %11s %243[^\r\n]", &l.frame, l.cmd, l.arg) < 2)
			continue;	// blank line or comment
		if (s.uiLineCount >= (Int)ARRAY_SIZE(s.uiLines))
		{
			++dropped;
			continue;
		}
		Bool ok = FALSE;
		for (size_t k = 0; k < ARRAY_SIZE(known); ++k)
			ok = ok || strcmp(l.cmd, known[k]) == 0;
		if (ok)
			s.uiLineCount++;
		else
			logLine("-aocUiScript: unknown command '%s' skipped (select, selectall, press, back, dock, log, bank, count, screenshot)", l.cmd);
	}
	fclose(f);
	if (dropped > 0)
		logLine("-aocUiScript: only %d lines are kept; %d more dropped", (int)ARRAY_SIZE(s.uiLines), dropped);
	s.anyFlag = TRUE;
	logLine("-aocUiScript: %d lines from '%s'", s.uiLineCount, args[1]);
	return 2;
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

	s.sawLogicFrame = TRUE;	// F5 -aocUiScript runs a line once its frame has run
	s.lastLogicFrame = frame;

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

void AoCHarness::onUnitQueueRefused( const Player *player, Int canMakeType, const ThingTemplate *unitType )
{
	HarnessState &s = state();
	if (!s.anyFlag || player == nullptr)
		return;
	const Int index = (Int)player->getPlayerIndex();
	if (index < 0 || index >= MAX_PLAYER_COUNT || canMakeType < 0 || canMakeType >= AOC_HARNESS_CANMAKE_SLOTS)
		return;
	s.queueRefusals[index][canMakeType]++;
	if (canMakeType != CANMAKE_NO_HOUSING || unitType == nullptr)
		return;
	for (Int n = 0; n < AOC_HARNESS_BLOCKED_UNITS; ++n)
	{
		if (s.housingRefused[index][n] == unitType)
			return;
		if (s.housingRefused[index][n] == nullptr)
		{
			s.housingRefused[index][n] = unitType;
			return;
		}
	}
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

	// F5: -aocUiScript, client UI steps between two logic frames.
	runUiScript();

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
