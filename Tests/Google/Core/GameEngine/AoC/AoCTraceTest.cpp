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

// AoC F12b: the pure part of -crcEvery, -crcWindow and -aocRandomTrace (design D11 section 2.12): which frames get a
// -crcLog line, the line formats (stock "frame,CRC" without the flags, "frame,CRC,SEED" with them), the argument
// rules, and the random trace's line and marker formats.

#include <string.h>
#include <string>
#include <gtest/gtest.h>

#include "AoC/AoCTrace.h"

namespace
{

AoCTrace::CrcSchedule schedule( UnsignedInt every, UnsignedInt from, UnsignedInt to )
{
	AoCTrace::CrcSchedule s;
	s.every = every;
	s.windowFrom = from;
	s.windowTo = to;
	return s;
}

std::string crcLine( const AoCTrace::CrcSchedule &s, UnsignedInt frame, UnsignedInt crc, UnsignedInt seed )
{
	char buf[64];
	const Int len = AoCTrace::formatCrcLine(buf, sizeof(buf), s, frame, crc, seed);
	EXPECT_EQ((size_t)len, strlen(buf));
	return std::string(buf);
}

}

TEST(AoCTrace, WithoutTheFlagsTheEngineIntervalDecides)
{
	const AoCTrace::CrcSchedule stock = schedule(0, 0, 0);
	EXPECT_TRUE(AoCTrace::crcLineDue(stock, 100, TRUE));
	EXPECT_FALSE(AoCTrace::crcLineDue(stock, 101, FALSE));
	EXPECT_FALSE(AoCTrace::crcLineDue(stock, 200, FALSE));	// whatever the frame, only where the engine computed it
	EXPECT_FALSE(AoCTrace::hasSeedColumn(stock));
	EXPECT_EQ("5400,439D8B85\n", crcLine(stock, 5400, 0x439D8B85, 0x12345678));	// the F1 format, byte for byte
}

TEST(AoCTrace, EveryNReplacesTheEngineInterval)
{
	const AoCTrace::CrcSchedule every1 = schedule(1, 0, 0);
	EXPECT_TRUE(AoCTrace::crcLineDue(every1, 1, FALSE));
	EXPECT_TRUE(AoCTrace::crcLineDue(every1, 6101, FALSE));
	EXPECT_FALSE(AoCTrace::crcLineDue(every1, 0, FALSE));

	const AoCTrace::CrcSchedule every300 = schedule(300, 0, 0);
	EXPECT_TRUE(AoCTrace::crcLineDue(every300, 300, FALSE));
	EXPECT_TRUE(AoCTrace::crcLineDue(every300, 600, TRUE));
	EXPECT_FALSE(AoCTrace::crcLineDue(every300, 100, TRUE));	// the engine's frame is not a multiple of 300
	EXPECT_FALSE(AoCTrace::crcLineDue(every300, 301, FALSE));
	EXPECT_TRUE(AoCTrace::hasSeedColumn(every300));
}

TEST(AoCTrace, TheWindowAddsEveryFrameInsideIt)
{
	const AoCTrace::CrcSchedule window = schedule(0, 6000, 6600);
	EXPECT_TRUE(AoCTrace::crcLineDue(window, 6000, FALSE));
	EXPECT_TRUE(AoCTrace::crcLineDue(window, 6377, FALSE));
	EXPECT_TRUE(AoCTrace::crcLineDue(window, 6600, FALSE));
	EXPECT_FALSE(AoCTrace::crcLineDue(window, 6601, FALSE));
	EXPECT_FALSE(AoCTrace::crcLineDue(window, 5999, FALSE));
	EXPECT_TRUE(AoCTrace::crcLineDue(window, 5900, TRUE));	// the engine's lines outside the window stay
	EXPECT_TRUE(AoCTrace::hasSeedColumn(window));

	const AoCTrace::CrcSchedule both = schedule(1000, 6000, 6010);
	EXPECT_TRUE(AoCTrace::crcLineDue(both, 6005, FALSE));
	EXPECT_TRUE(AoCTrace::crcLineDue(both, 7000, FALSE));
	EXPECT_FALSE(AoCTrace::crcLineDue(both, 6100, TRUE));
}

TEST(AoCTrace, LinesWithTheFlagsCarryTheSeed)
{
	EXPECT_EQ("6101,0ABCDEF0,00000001\n", crcLine(schedule(1, 0, 0), 6101, 0x0ABCDEF0, 1));
	EXPECT_EQ("6200,FFFFFFFF,DEADBEEF\n", crcLine(schedule(0, 6000, 6600), 6200, 0xFFFFFFFF, 0xDEADBEEF));

	char tiny[8];
	EXPECT_EQ(0, AoCTrace::formatCrcLine(tiny, sizeof(tiny), schedule(1, 0, 0), 6101, 1, 2));
	EXPECT_STREQ("", tiny);
}

TEST(AoCTrace, ArgumentRules)
{
	UnsignedInt n = 7;
	EXPECT_TRUE(AoCTrace::parseCount("1", n));
	EXPECT_EQ(1u, n);
	EXPECT_TRUE(AoCTrace::parseCount("4000000000", n));
	EXPECT_EQ(4000000000u, n);
	n = 7;
	EXPECT_FALSE(AoCTrace::parseCount("0", n));
	EXPECT_FALSE(AoCTrace::parseCount("", n));
	EXPECT_FALSE(AoCTrace::parseCount(nullptr, n));
	EXPECT_FALSE(AoCTrace::parseCount("-5", n));
	EXPECT_FALSE(AoCTrace::parseCount("12x", n));
	EXPECT_FALSE(AoCTrace::parseCount("4000000001", n));
	EXPECT_FALSE(AoCTrace::parseCount("99999999999999999999999", n));
	EXPECT_EQ(7u, n);	// left alone on refusal

	UnsignedInt from = 3;
	UnsignedInt to = 4;
	EXPECT_TRUE(AoCTrace::parseWindow("6000", "6600", from, to));
	EXPECT_EQ(6000u, from);
	EXPECT_EQ(6600u, to);
	EXPECT_TRUE(AoCTrace::parseWindow("6100", "6100", from, to));	// one frame
	EXPECT_FALSE(AoCTrace::parseWindow("6600", "6000", from, to));	// reversed
	EXPECT_FALSE(AoCTrace::parseWindow("0", "10", from, to));
	EXPECT_FALSE(AoCTrace::parseWindow("10", "", from, to));
	EXPECT_EQ(6100u, from);
	EXPECT_EQ(6100u, to);
}

TEST(AoCTrace, WindowBounds)
{
	EXPECT_FALSE(AoCTrace::inWindow(0, 0, 0));	// no window
	EXPECT_FALSE(AoCTrace::inWindow(1, 0, 1));
	EXPECT_TRUE(AoCTrace::inWindow(10, 20, 10));
	EXPECT_TRUE(AoCTrace::inWindow(10, 20, 20));
	EXPECT_FALSE(AoCTrace::inWindow(10, 20, 9));
	EXPECT_FALSE(AoCTrace::inWindow(10, 20, 21));
}

TEST(AoCTrace, RandomTraceLines)
{
	EXPECT_STREQ("AIUpdate.cpp", AoCTrace::baseName("C:\\work\\engine\\GeneralsMD\\Code\\GameEngine\\Source\\GameLogic\\Object\\Update\\AIUpdate.cpp"));
	EXPECT_STREQ("Weapon.cpp", AoCTrace::baseName("/src/GameLogic/Object/Weapon.cpp"));
	EXPECT_STREQ("plain.cpp", AoCTrace::baseName("plain.cpp"));
	EXPECT_STREQ("", AoCTrace::baseName(nullptr));

	char buf[256];
	EXPECT_GT(AoCTrace::formatRandomInt(buf, sizeof(buf), 6101, TRUE, 'I', "C:\\x\\Weapon.cpp", 2695, 0, 99, 42), 0);
	EXPECT_STREQ("6101,L,I,Weapon.cpp:2695,0,99,42\n", buf);
	EXPECT_GT(AoCTrace::formatRandomInt(buf, sizeof(buf), 6101, FALSE, 'u', "a/AudioEventRTS.cpp", 343, 0, 3, -1), 0);
	EXPECT_STREQ("6101,C,u,AudioEventRTS.cpp:343,0,3,-1\n", buf);
	EXPECT_GT(AoCTrace::formatRandomReal(buf, sizeof(buf), 6200, TRUE, 'R', "PhysicsUpdate.cpp", 77, 0.0f, 1.0f, 0.25f), 0);
	EXPECT_STREQ("6200,L,R,PhysicsUpdate.cpp:77,0,1,0.25\n", buf);
	EXPECT_GT(AoCTrace::formatRandomReal(buf, sizeof(buf), 6200, TRUE, 'r', "x.cpp", 1, 0.1f, 2.5f, 1.0f / 3.0f), 0);
	EXPECT_STREQ("6200,L,r,x.cpp:1,0.100000001,2.5,0.333333343\n", buf);	// 9 digits: a float round-trips
	EXPECT_GT(AoCTrace::formatCrcPoint(buf, sizeof(buf), 6101, 0x00C0FFEE), 0);
	EXPECT_STREQ("#6101,crc-point,00C0FFEE\n", buf);

	char tiny[10];
	EXPECT_EQ(0, AoCTrace::formatRandomInt(tiny, sizeof(tiny), 6101, TRUE, 'I', "Weapon.cpp", 2695, 0, 99, 42));
	EXPECT_STREQ("", tiny);
}
