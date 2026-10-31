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

// AoCSimRate.h
// Age of Commanders: selectable simulation rate, 30 or 60 logic frames per second (design D11 F15).
//
// One binary, rate chosen at startup: Options.ini "SimulationRate = 30|60" (default 60 when absent; 30 for the
// base Generals target, which has none of the 60 Hz corrections), overridden by "-simrate <30|60>". The value
// is read before any INI loads, so every INI millisecond-to-frame conversion uses it. LOGICFRAMES_PER_SECOND
// (Common/GameCommon.h) is a read of AoCSimRate::framesPerSecond(); MAX_LOGICFRAMES_PER_SECOND is the constexpr
// bound for array sizes.
//
// Lockstep: the rate travels in the game options string (key "AF=", written only when the rate is not 30, so a
// 30 Hz game stays readable by stock builds) next to the slot list, is recorded in the replay header through
// that string, is written by GameLogic::xfer, and a mismatch is refused at the LAN join, at replay playback and
// at save load. A stock options string without the key is a 30 Hz game, which is what every stock replay is.
// At any rate but 30 the rate is also folded into the executable CRC (exeCRCForRate), which the GameSpy and
// GeneralsOnline lobbies compare.
//
// Legacy frame: data that counts retail 30 Hz frames (particle keyframes, script counters) keeps that
// cadence through legacyFrame() and isLegacyFrameStep(); at 30 Hz both are the identity.

#pragma once

#include "Common/GameCommon.h"

class UnicodeString;

namespace AoCSimRate
{
	enum
	{
		MinRate = 30,
		MaxRate = MAX_LOGICFRAMES_PER_SECOND,
#if RTS_GENERALS
		// The base Generals target has none of the frame corrections, refusals or save field of the Zero Hour
		// target, so it keeps the stock rate unless -simrate or Options.ini asks otherwise.
		DefaultRate = 30,
#else
		DefaultRate = 60,
#endif
	};

	// framesPerSecond() and frameMultiplier() are declared in Common/GameCommon.h so that
	// LOGICFRAMES_PER_SECOND can read them from every translation unit.

	inline Bool isValidRate( Int rate ) { return rate == MinRate || rate == MaxRate; }

	/// Retail frame number for frame-count data authored at 30 Hz. Identity at 30 Hz.
	inline UnsignedInt legacyFrame( UnsignedInt frame ) { return frame / (UnsignedInt)frameMultiplier(); }

	/// TRUE on the logic frames where the retail 30 Hz cadence steps (every frame at 30 Hz, every other frame at 60 Hz).
	inline Bool isLegacyFrameStep( UnsignedInt frame ) { return (frame % (UnsignedInt)frameMultiplier()) == 0; }

	/// Convert a frame count authored at 30 Hz to frames at the live rate.
	inline Int legacyFramesToFrames( Int legacyFrames ) { return legacyFrames * frameMultiplier(); }

	// Stock update intervals of one second or a fraction of it that were compile-time constants (an enum or a
	// namespace-scope const). They read the live rate, so they keep their real-time length, and at 30 Hz they are
	// the stock values. Functions rather than macros, so the stock names do not leak into every including file.
	inline Int enemyScanFrames() { return framesPerSecond(); }			///< AIHuntState, AIAttackAreaState (was ENEMY_SCAN_RATE)
	inline Int airfieldCheckFrames() { return framesPerSecond(); }		///< JetOrHeliCirclingDeadAirfieldState (was HOW_OFTEN_TO_CHECK)
	inline Int slavedUpdateFrames() { return framesPerSecond() / 4; }	///< SlavedUpdate (was SLAVED_UPDATE_RATE)
	inline Int spawnUpdateFrames() { return framesPerSecond() / 2; }	///< SpawnBehavior (was SPAWN_UPDATE_RATE)

	/// Set the rate; returns FALSE (and keeps the old value) when the rate is not 30 or 60, or when the rate is
	/// already locked. 'source' is for the log line.
	Bool set( Int rate, const char *source );

	/// Read "SimulationRate" from Options.ini. Needs TheGlobalData (user data path); called from
	/// CommandLine::parseCommandLineForStartup right after the GlobalData is created, before -simrate is parsed.
	void readStartupOptions();

	/// Fix the rate for the rest of the process. Called by CommandLine::parseCommandLineForStartup once Options.ini
	/// and -simrate have been read, before any INI loads: every later set() is refused.
	void lock();

	/// TRUE once lock() has run; the rate is then final for this process.
	Bool isLocked();

	/// The executable CRC this process reports (lobbies, GameSpy staging rooms, GeneralsOnline login and
	/// matchmaking, replay headers). At 30 Hz it is the stock CRC; at any other rate the rate is folded in, so
	/// every CRC gate that compares executables also keeps the two rates apart.
	UnsignedInt exeCRCForRate( UnsignedInt exeCRC, Int rate );

	// Mismatch reporting, shared by the replay, save and lobby refusals.
	void logMismatch( const char *what, Int otherRate );
	UnicodeString mismatchMessage( const char *what, Int otherRate );

	/// Replay playback (RecorderClass::playbackFile, every caller): TRUE when a replay recorded at replayRate cannot
	/// play at this rate. The refusal is then reported once: stderr, a message box when there is a window, and exit
	/// code 5 under the harness flags.
	Bool refuseReplay( Int replayRate );

	/// Save load: GameLogic::xfer notes the rate of a refused save; the save/load menu reads it once.
	void noteSaveMismatch( Int savedRate );
	Int consumeSaveMismatch();	///< 0 when the last load failure was not a rate mismatch; clears the pending note
	Int lastSaveMismatchRate();	///< the rate of the most recently refused save, for the message
}
