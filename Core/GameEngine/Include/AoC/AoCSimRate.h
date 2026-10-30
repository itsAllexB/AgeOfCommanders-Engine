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
// One binary, rate chosen at startup: Options.ini "SimulationRate = 30|60" (default 60 when absent),
// overridden by "-simrate <30|60>". The value is read before any INI loads, so every INI
// millisecond-to-frame conversion uses it. LOGICFRAMES_PER_SECOND (Common/GameCommon.h) is a read of
// AoCSimRate::framesPerSecond(); MAX_LOGICFRAMES_PER_SECOND is the constexpr bound for array sizes.
//
// Lockstep: the rate travels in the game options string (key "AF=") next to the slot list, is recorded
// in the replay header through that string, is written by GameLogic::xfer, and a mismatch is refused
// at the LAN join, at replay playback and at save load. A stock options string without the key is a
// 30 Hz game, which is what every stock replay is.
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
		DefaultRate = 60,
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

	/// Set the rate; returns FALSE (and keeps the old value) when the rate is not 30 or 60. 'source' is for the log line.
	Bool set( Int rate, const char *source );

	/// Read "SimulationRate" from Options.ini. Needs TheGlobalData (user data path); called from
	/// CommandLine::parseCommandLineForStartup right after the GlobalData is created, before -simrate is parsed.
	void readStartupOptions();

	/// TRUE once set() or readStartupOptions() has run; after that the rate is final for this process.
	Bool isLocked();

	// Mismatch reporting, shared by the replay, save and lobby refusals.
	void logMismatch( const char *what, Int otherRate );
	UnicodeString mismatchMessage( const char *what, Int otherRate );

	/// Save load: GameLogic::xfer notes the rate of a refused save; the save/load menu reads it once.
	void noteSaveMismatch( Int savedRate );
	Int consumeSaveMismatch();	///< 0 when the last load failure was not a rate mismatch; clears the pending note
	Int lastSaveMismatchRate();	///< the rate of the most recently refused save, for the message
}
