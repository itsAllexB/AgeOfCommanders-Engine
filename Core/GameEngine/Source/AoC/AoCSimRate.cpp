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

// AoCSimRate.cpp
// Age of Commanders: selectable simulation rate (design D11 F15). See AoCSimRate.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCSimRate.h"

#include "Common/crc.h"
#include "GameClient/GameText.h"
#include "Common/GlobalData.h"
#include "Common/OptionPreferences.h"
#include "Common/UnicodeString.h"
#include "GameNetwork/NetworkDefs.h"

#include <stdio.h>

// The live value. Read by LOGICFRAMES_PER_SECOND through AoCSimRate::framesPerSecond() (Common/GameCommon.h).
Int AoCSimRate::g_framesPerSecond = AoCSimRate::DefaultRate;

namespace
{
	Bool s_locked = FALSE;
	Int s_saveMismatchRate = 0;
	Int s_lastSaveMismatchRate = 0;

	// Stock network frame constants are 30 Hz frame counts (NetworkUtil.cpp). They are plain globals,
	// so they are rescaled here to keep the same real-time windows at 60 Hz.
	void applyNetworkFrameConstants()
	{
		const Int mult = AoCSimRate::frameMultiplier();
		MIN_LOGIC_FRAMES = 5 * mult;
		MAX_FRAMES_AHEAD = 128 * mult;
		MIN_RUNAHEAD = 4 * mult;
		FRAME_DATA_LENGTH = (MAX_FRAMES_AHEAD + 1) * 2;
		FRAMES_TO_KEEP = (MAX_FRAMES_AHEAD / 2) + 1;
	}

	void mirrorIntoGlobalData( Int oldRate, Int newRate )
	{
		if (TheWritableGlobalData == nullptr)
			return;
		TheWritableGlobalData->m_logicFramesPerSecond = newRate;
		// Defaults the GlobalData constructor derived from the rate before Options.ini could be read.
		if (TheWritableGlobalData->m_skirmishTickRate == oldRate)
			TheWritableGlobalData->m_skirmishTickRate = newRate;
		if (TheWritableGlobalData->m_defaultOcclusionDelay == (UnsignedInt)(oldRate * 3))
			TheWritableGlobalData->m_defaultOcclusionDelay = newRate * 3;
	}
}

Bool AoCSimRate::set( Int rate, const char *source )
{
	if (isLocked())
	{
		// INI millisecond-to-frame conversions and the network frame constants already use the locked rate.
		fprintf(stderr, "[AoC] %s: the simulation rate is fixed at %d Hz once startup has read it; ignoring %d\n", source ? source : "SimulationRate", g_framesPerSecond, rate);
		fflush(stderr);
		return FALSE;
	}
	if (!isValidRate(rate))
	{
		fprintf(stderr, "[AoC] %s: simulation rate must be 30 or 60, not %d; keeping %d Hz\n", source ? source : "SimulationRate", rate, g_framesPerSecond);
		fflush(stderr);
		return FALSE;
	}
	const Int oldRate = g_framesPerSecond;
	g_framesPerSecond = rate;
	applyNetworkFrameConstants();
	mirrorIntoGlobalData(oldRate, rate);
	fprintf(stderr, "[AoC] simulation rate %d Hz (%s)\n", rate, source ? source : "default");
	fflush(stderr);
	return TRUE;
}

void AoCSimRate::readStartupOptions()
{
	Int rate = DefaultRate;
	const char *source = "default";
	if (TheGlobalData != nullptr)
	{
		// OptionPreferences reads Options.ini with plain fopen from the user data folder, so it works
		// before any subsystem exists. An absent or invalid key is the default rate.
		OptionPreferences prefs;
		const Int fromIni = prefs.getSimulationRate();
		if (fromIni != 0)
		{
			rate = fromIni;
			source = "Options.ini SimulationRate";
		}
	}
	if (!set(rate, source))
		set(DefaultRate, "default after invalid Options.ini SimulationRate");
}

void AoCSimRate::lock()
{
	s_locked = TRUE;
}

Bool AoCSimRate::isLocked()
{
	return s_locked;
}

UnsignedInt AoCSimRate::exeCRCForRate( UnsignedInt exeCRC, Int rate )
{
	if (rate == BaseFps)
		return exeCRC;
	CRC crc;
	crc.computeCRC(&exeCRC, sizeof(exeCRC));
	crc.computeCRC(&rate, sizeof(rate));
	return crc.get();
}

void AoCSimRate::logMismatch( const char *what, Int otherRate )
{
	fprintf(stderr, "[AoC] %s was made at %d Hz but this game runs at %d Hz; set SimulationRate = %d in Options.ini or start with -simrate %d\n",
		what, otherRate, g_framesPerSecond, otherRate, otherRate);
	fflush(stderr);
}

UnicodeString AoCSimRate::mismatchMessage( const char *what, Int otherRate )
{
	static const WideChar *const defaultFormat =
		L"%ls was made at a %d Hz simulation rate, but this game runs at %d Hz.\n\nSet SimulationRate = %d in Options.ini (or start with -simrate %d) and restart to use it.";
	UnicodeString whatW;
	whatW.translate(AsciiString(what));
	UnicodeString format = TheGameText ? TheGameText->FETCH_OR_SUBSTITUTE("GUI:AoCSimRateMismatch", defaultFormat) : UnicodeString(defaultFormat);
	UnicodeString msg;
	msg.format(format.str(), whatW.str(), otherRate, g_framesPerSecond, otherRate, otherRate);
	return msg;
}

void AoCSimRate::noteSaveMismatch( Int savedRate )
{
	s_saveMismatchRate = savedRate;
	s_lastSaveMismatchRate = savedRate;
}

Int AoCSimRate::lastSaveMismatchRate()
{
	return s_lastSaveMismatchRate;
}

Int AoCSimRate::consumeSaveMismatch()
{
	const Int rate = s_saveMismatchRate;
	s_saveMismatchRate = 0;
	return rate;
}
