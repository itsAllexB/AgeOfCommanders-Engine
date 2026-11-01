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

// AoC F11: the pure timer of the Airlift Contract's timed drop (design D11 section 2.11): arming by the upgrade, the first
// start of the timer (no drop), the drop and the reset, construction and the disabled shift (OCLUpdate's timer), the
// countdown, the one-second poll at 30 and 60 Hz, the chip text, and the save round trip and CRC of the three fields.

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "AoC/AoCSimRate.h"
#include "AoC/AoCTimedDropUpdate.h"
#include "Common/GameCommon.h"
#include "Common/Snapshot.h"
#include "Common/XferCRC.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"

namespace
{

// The fields through the very calls the module makes: its xfer writes the version byte and the UpdateModule block, then
// AoCTimedDropState::xfer; its crc calls AoCTimedDropState::crc.
class StateHolder : public Snapshot
{
public:
	AoCTimedDropState state;
protected:
	virtual void crc( Xfer *xfer ) override { state.crc(xfer); }
	virtual void xfer( Xfer *xfer ) override { state.xfer( xfer, AoCTimedDropState::XFER_VERSION ); }
	virtual void loadPostProcess() override {}
};

class AoCTimedDropTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_startRate = AoCSimRate::g_framesPerSecond;
		AoCSimRate::g_framesPerSecond = 30;
	}

	void TearDown() override
	{
		AoCSimRate::g_framesPerSecond = m_startRate;
	}

	// The module's armed update with a fixed delay: the delay is passed only when the timer (re)starts, as the module draws it.
	static AoCTimedDropStep step( AoCTimedDropState &s, UnsignedInt now, UnsignedInt delay, Bool underConstruction = FALSE, Bool disabled = FALSE )
	{
		const UnsignedInt d = AoCTimedDrop::needsDelay(s, now, underConstruction, disabled) ? delay : 0;
		return AoCTimedDrop::armedUpdate(s, now, underConstruction, disabled, d);
	}

	static UnsignedInt crcOf( StateHolder &holder )
	{
		XferCRC xfer;
		xfer.open("lightCRC");
		xfer.xferSnapshot(&holder);
		UnsignedInt value = xfer.getCRC();
		xfer.close();
		return value;
	}

	Int m_startRate;
};

}	// namespace

TEST_F(AoCTimedDropTest, ArmsOnceWhenTheUpgradeIsThere)
{
	AoCTimedDropState s;
	EXPECT_FALSE(s.armed);
	EXPECT_FALSE(AoCTimedDrop::arm(s, FALSE)) << "no upgrade, no arming: the module polls again in a second";
	EXPECT_FALSE(s.armed);
	EXPECT_TRUE(AoCTimedDrop::arm(s, TRUE)) << "the poll that sees the upgrade arms it";
	EXPECT_TRUE(s.armed);
	EXPECT_FALSE(AoCTimedDrop::arm(s, TRUE)) << "TRUE only once (the [AoC drop] armed line)";
	EXPECT_FALSE(AoCTimedDrop::arm(s, FALSE));
	EXPECT_TRUE(s.armed) << "armed for good, whatever the upgrade does later";
	EXPECT_EQ(s.nextDropFrame, 0u) << "arming does not start the timer by itself";
}

TEST_F(AoCTimedDropTest, FirstStartThenDropEveryDelay)
{
	// The stock Drop Zone: six crates every 120 s, 3600 frames at 30 Hz. Armed at frame 300.
	const UnsignedInt delay = 3600;
	AoCTimedDropState s;
	AoCTimedDrop::arm(s, TRUE);
	EXPECT_EQ(step(s, 300, delay), AOC_TIMED_DROP_STARTED) << "the first try starts a timer and drops nothing (OCLUpdate)";
	EXPECT_EQ(s.timerStartFrame, 300u);
	EXPECT_EQ(s.nextDropFrame, 3900u);
	EXPECT_EQ(step(s, 301, delay), AOC_TIMED_DROP_WAIT);
	EXPECT_EQ(step(s, 3899, delay), AOC_TIMED_DROP_WAIT);
	EXPECT_EQ(step(s, 3900, delay), AOC_TIMED_DROP_DROP) << "the first drop, one delay after the start";
	EXPECT_EQ(s.timerStartFrame, 3900u) << "and the timer resets";
	EXPECT_EQ(s.nextDropFrame, 7500u);
	EXPECT_EQ(step(s, 7500, delay), AOC_TIMED_DROP_DROP);
	EXPECT_EQ(s.nextDropFrame, 11100u) << "every 3600 frames";

	// Frame by frame from the start: exactly one drop per delay.
	AoCTimedDropState f;
	AoCTimedDrop::arm(f, TRUE);
	Int drops = 0;
	for (UnsignedInt now = 1; now <= 1 + 3 * delay; ++now)
		if (step(f, now, delay) == AOC_TIMED_DROP_DROP)
			++drops;
	EXPECT_EQ(drops, 3);
}

TEST_F(AoCTimedDropTest, WaitsForConstructionAndShiftsWhileDisabled)
{
	const UnsignedInt delay = 3600;
	AoCTimedDropState s;
	AoCTimedDrop::arm(s, TRUE);	// a player upgrade can be there before the building stands
	EXPECT_EQ(step(s, 100, delay, TRUE), AOC_TIMED_DROP_WAIT) << "no timer while under construction";
	EXPECT_EQ(step(s, 101, delay, TRUE, TRUE), AOC_TIMED_DROP_WAIT) << "nor while subdued under construction (the TheSuperHackers fix)";
	EXPECT_EQ(s.nextDropFrame, 0u);
	EXPECT_EQ(step(s, 1450, delay), AOC_TIMED_DROP_STARTED) << "the timer starts when the building is complete";
	EXPECT_EQ(s.nextDropFrame, 5050u);

	// Disabled with the timer running: both frames move on by one per frame, so the drop slides by the time spent disabled.
	for (UnsignedInt now = 2000; now < 2100; ++now)
		EXPECT_EQ(step(s, now, delay, FALSE, TRUE), AOC_TIMED_DROP_SHIFTED);
	EXPECT_EQ(s.timerStartFrame, 1550u);
	EXPECT_EQ(s.nextDropFrame, 5150u);
	EXPECT_EQ(step(s, 5149, delay), AOC_TIMED_DROP_WAIT);
	EXPECT_EQ(step(s, 5150, delay), AOC_TIMED_DROP_DROP);

	// Disabled exactly on the drop frame: shifted, not dropped.
	EXPECT_EQ(step(s, 8750, delay, FALSE, TRUE), AOC_TIMED_DROP_SHIFTED);
	EXPECT_EQ(s.nextDropFrame, 8751u);
	EXPECT_EQ(step(s, 8751, delay), AOC_TIMED_DROP_DROP);
}

TEST_F(AoCTimedDropTest, NeedsDelayOnlyWhenTheTimerRestarts)
{
	AoCTimedDropState s;
	EXPECT_TRUE(AoCTimedDrop::needsDelay(s, 10, FALSE, FALSE)) << "the first start draws a delay";
	EXPECT_FALSE(AoCTimedDrop::needsDelay(s, 10, TRUE, FALSE)) << "not under construction";
	AoCTimedDrop::armedUpdate(s, 10, FALSE, FALSE, 50);
	EXPECT_FALSE(AoCTimedDrop::needsDelay(s, 11, FALSE, FALSE)) << "nothing is drawn between drops";
	EXPECT_FALSE(AoCTimedDrop::needsDelay(s, 60, FALSE, TRUE)) << "nor while disabled";
	EXPECT_TRUE(AoCTimedDrop::needsDelay(s, 60, FALSE, FALSE)) << "the drop restarts the timer";
}

TEST_F(AoCTimedDropTest, CountdownAndPollFollowTheRate)
{
	AoCTimedDropState s;
	EXPECT_EQ(AoCTimedDrop::remainingFrames(s, 100), 0u) << "no timer yet";
	EXPECT_FLOAT_EQ(AoCTimedDrop::countdownPercent(s, 100), 0.0f);
	AoCTimedDrop::armedUpdate(s, 300, FALSE, FALSE, 3600);
	EXPECT_EQ(AoCTimedDrop::remainingFrames(s, 300), 3600u);
	EXPECT_EQ(AoCTimedDrop::remainingFrames(s, 2100), 1800u);
	EXPECT_EQ(AoCTimedDrop::remainingFrames(s, 3900), 0u);
	EXPECT_EQ(AoCTimedDrop::remainingFrames(s, 4000), 0u) << "never wraps past the drop frame";
	EXPECT_FLOAT_EQ(AoCTimedDrop::countdownPercent(s, 2100), 0.5f);
	EXPECT_FLOAT_EQ(AoCTimedDrop::countdownPercent(s, 3900), 1.0f);

	AoCSimRate::g_framesPerSecond = 30;
	EXPECT_EQ(AoCTimedDrop::pollFrames(), 30u) << "one second at 30 Hz";
	EXPECT_EQ(AoCTimedDrop::chipText(3060).compare(UnicodeString(L"Next airdrop 1:42")), 0) << "102 s";
	EXPECT_EQ(AoCTimedDrop::chipText(3059).compare(UnicodeString(L"Next airdrop 1:42")), 0) << "a started second shows whole";
	EXPECT_EQ(AoCTimedDrop::chipText(0).compare(UnicodeString(L"Next airdrop 0:00")), 0);
	AoCSimRate::g_framesPerSecond = 60;
	EXPECT_EQ(AoCTimedDrop::pollFrames(), 60u) << "one second at 60 Hz";
	EXPECT_EQ(AoCTimedDrop::chipText(6120).compare(UnicodeString(L"Next airdrop 1:42")), 0) << "the same 102 s at 60 Hz";
}

TEST_F(AoCTimedDropTest, StateRoundTripsThroughItsXferAndCrc)
{
	const std::string path = ::testing::TempDir() + "AoCTimedDropStateRoundTrip.sav";
	{
		StateHolder holder;
		holder.state.nextDropFrame = 7500;
		holder.state.timerStartFrame = 3900;
		holder.state.armed = TRUE;
		XferSave save;
		save.open(path.c_str());
		save.xferSnapshot(&holder);
		save.close();
	}
	{
		FILE *f = fopen(path.c_str(), "rb");
		ASSERT_NE(f, nullptr);
		fseek(f, 0, SEEK_END);
		const long size = ftell(f);
		fclose(f);
		EXPECT_EQ(size, (long)(2 * sizeof(UnsignedInt) + sizeof(Bool))) << "the three fields and nothing else";

		StateHolder loaded;
		XferLoad load;
		load.setOptions(XO_NO_POST_PROCESSING);
		load.open(path.c_str());
		load.xferSnapshot(&loaded);
		load.close();
		EXPECT_EQ(loaded.state.nextDropFrame, 7500u) << "the drop comes at the same frame after a load";
		EXPECT_EQ(loaded.state.timerStartFrame, 3900u);
		EXPECT_TRUE(loaded.state.armed);
	}
	remove(path.c_str());

	// The CRC covers each field.
	StateHolder a;
	a.state.nextDropFrame = 7500;
	a.state.timerStartFrame = 3900;
	a.state.armed = TRUE;
	const UnsignedInt base = crcOf(a);
	a.state.nextDropFrame = 7501;
	EXPECT_NE(crcOf(a), base) << "the next-drop frame";
	a.state.nextDropFrame = 7500;
	a.state.timerStartFrame = 3901;
	EXPECT_NE(crcOf(a), base) << "the timer-start frame";
	a.state.timerStartFrame = 3900;
	a.state.armed = FALSE;
	EXPECT_NE(crcOf(a), base) << "armed";
	a.state.armed = TRUE;
	EXPECT_EQ(crcOf(a), base);
}
