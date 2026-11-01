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

// AoC F8: per-builder construction speed (design D11 section 2.8). The one multiplication point (1000 leaves the stock
// increment bit for bit, 600 and 1500 scale it), the construction line run frame by frame as the dozer build state runs
// it (completion frames at D5's speeds, two builders on a 60-second building in 24 seconds at 30 and 60 Hz), the
// AoCBuildSpeedPerMille parser, and the bookkeeping of the harness build log.

#include <string.h>
#include <gtest/gtest.h>

#include "AoC/AoCBuildSpeed.h"
#include "Common/INI.h"
#include "WWMath/wwmath.h"

namespace
{

/// The stock per-frame construction increment of DozerActionDoActionState::update for a structure of framesToBuild.
Real stockIncrement( Int framesToBuild )
{
	return WWMath::Div_FixNaN(100.0f, (Real)framesToBuild, 100.0f);
}

/// Runs the construction line frame by frame, as the dozer build state does: every working builder adds its scaled
/// increment in turn (the update order), and the structure is complete on the frame its percent reaches 100. Returns
/// the number of frames from the first construction frame to that one, both counted.
UnsignedInt framesToComplete( Int framesToBuild, const Int *perMille, Int builders )
{
	Real percent = 0.0f;
	for (UnsignedInt frame = 1; frame < 100000; ++frame)
	{
		for (Int b = 0; b < builders; ++b)
		{
			percent = percent + AoCBuildSpeed::scale(stockIncrement(framesToBuild), perMille[b]);
			if (percent >= 100.0f)
				return frame;
		}
	}
	return 0;
}

UnsignedInt framesToComplete( Int framesToBuild, Int perMille )
{
	return framesToComplete(framesToBuild, &perMille, 1);
}

// Drives a FieldParse procedure the way the INI loader does: the key token is consumed, the rest of the line is read
// with the static INI tokenizer.
void startLine( char *buffer, size_t size, const char *line )
{
	strncpy(buffer, line, size - 1);
	buffer[size - 1] = '\0';
	::strtok(buffer, INI::getSeps());
}

} // namespace

// Decision 1: the stock rate is the stock increment itself, not a multiplication by one.
TEST(AoCBuildSpeedTest, StockRateReturnsTheIncrementBitForBit)
{
	const Real increments[] = { stockIncrement(300), stockIncrement(601), stockIncrement(1350), 1000.0f / 300.0f, 3500.0f / 2700.0f,
		0.0f, 1.0e-30f, 123456.789f, -0.25f };
	for (size_t i = 0; i < sizeof(increments) / sizeof(increments[0]); ++i)
	{
		const Real scaled = AoCBuildSpeed::scale(increments[i], AOC_BUILD_SPEED_DEFAULT_PER_MILLE);
		EXPECT_EQ(0, memcmp(&scaled, &increments[i], sizeof(Real))) << "increment " << i;
	}
	EXPECT_EQ(AOC_BUILD_SPEED_DEFAULT_PER_MILLE, 1000);
}

// D5's speeds: the dozer and Foreman at 1500, the GLA worker at 600, as increment * perMille / 1000.0f.
TEST(AoCBuildSpeedTest, SpeedsScaleTheIncrement)
{
	const Real tenSecondsAt30Hz = stockIncrement(300);	// 100 / 300, a 10-second structure at 30 Hz
	EXPECT_EQ(AoCBuildSpeed::scale(tenSecondsAt30Hz, 1500), 0.5f);
	EXPECT_EQ(AoCBuildSpeed::scale(tenSecondsAt30Hz, 600), 0.2f);
	EXPECT_EQ(AoCBuildSpeed::scale(stockIncrement(600), 1500), 0.25f);
	EXPECT_EQ(AoCBuildSpeed::scale(stockIncrement(600), 600), 0.1f);
	EXPECT_EQ(AoCBuildSpeed::scale(0.25f, 2000), 0.5f);
	// the health increment (maxHealth / framesToBuild) by the same rule
	const Real health = 1000.0f / 300.0f;
	EXPECT_EQ(AoCBuildSpeed::scale(health, 1500), health * 1500.0f / 1000.0f);
	EXPECT_NEAR(AoCBuildSpeed::scale(health, 1500), 5.0f, 1.0e-5f);
	EXPECT_NEAR(AoCBuildSpeed::scale(health, 600), 2.0f, 1.0e-5f);
	// the stock arithmetic exactly, step by step in single precision
	const Real x = stockIncrement(1350);
	const Real product = x * 600.0f;
	EXPECT_EQ(AoCBuildSpeed::scale(x, 600), product / 1000.0f);
}

// The construction line frame by frame for one builder on a 10-second structure (the stock barracks, power plant and
// supply centre): 300 frames at 30 Hz at 1000, two thirds at 1500, five thirds at 600. The single-precision sum can
// end one frame later than the exact count, as in stock: 60 Hz at 1000 completes at 601, not 600.
TEST(AoCBuildSpeedTest, CompletionFramesOfOneBuilderAtD5Speeds)
{
	EXPECT_EQ(framesToComplete(300, 1000), 300u);
	EXPECT_EQ(framesToComplete(300, 1500), 200u);
	EXPECT_EQ(framesToComplete(300, 600), 501u);
	EXPECT_EQ(framesToComplete(600, 1000), 601u);
	EXPECT_EQ(framesToComplete(600, 1500), 400u);
	EXPECT_EQ(framesToComplete(600, 600), 1001u);
	// a 60-second structure
	EXPECT_EQ(framesToComplete(1800, 1000), 1800u);
	EXPECT_EQ(framesToComplete(1800, 1500), 1200u);
	EXPECT_EQ(framesToComplete(3600, 1000), 3600u);
}

// D11 2.8's test: two builders at 1000 and 1500 on a 60-second structure finish in 24 seconds (60 / 2.5), as frame
// counts at both rates, in either update order.
TEST(AoCBuildSpeedTest, TwoBuildersOnASixtySecondStructureFinishInTwentyFourSeconds)
{
	const Int speeds[] = { 1000, 1500 };
	const Int reversed[] = { 1500, 1000 };
	EXPECT_EQ(framesToComplete(60 * 30, speeds, 2), 24u * 30u);
	EXPECT_EQ(framesToComplete(60 * 30, reversed, 2), 24u * 30u);
	EXPECT_EQ(framesToComplete(60 * 60, speeds, 2), 24u * 60u);
	EXPECT_EQ(framesToComplete(60 * 60, reversed, 2), 24u * 60u);
	// one builder alone takes the stock 60 seconds and the dozer 40
	EXPECT_EQ(framesToComplete(60 * 30, 1000), 60u * 30u);
	EXPECT_EQ(framesToComplete(60 * 30, 1500), 40u * 30u);
}

// A value below 1 is an INI error; a refused line leaves the field as it was.
TEST(AoCBuildSpeedTest, PerMilleParsesAWholeNumberOfAtLeastOne)
{
	char buffer[256];
	Int value = AOC_BUILD_SPEED_DEFAULT_PER_MILLE;
	startLine(buffer, sizeof(buffer), "AoCBuildSpeedPerMille = 600");
	AoCBuildSpeed::parsePerMille(nullptr, nullptr, &value, nullptr);
	EXPECT_EQ(value, 600);
	startLine(buffer, sizeof(buffer), "AoCBuildSpeedPerMille = 1500");
	AoCBuildSpeed::parsePerMille(nullptr, nullptr, &value, nullptr);
	EXPECT_EQ(value, 1500);
	startLine(buffer, sizeof(buffer), "AoCBuildSpeedPerMille = 1");
	AoCBuildSpeed::parsePerMille(nullptr, nullptr, &value, nullptr);
	EXPECT_EQ(value, 1);

	value = 600;
	startLine(buffer, sizeof(buffer), "AoCBuildSpeedPerMille = 0");
	EXPECT_THROW(AoCBuildSpeed::parsePerMille(nullptr, nullptr, &value, nullptr), decltype(INI_INVALID_DATA)) << "zero";
	startLine(buffer, sizeof(buffer), "AoCBuildSpeedPerMille = -1000");
	EXPECT_THROW(AoCBuildSpeed::parsePerMille(nullptr, nullptr, &value, nullptr), decltype(INI_INVALID_DATA)) << "negative";
	EXPECT_EQ(value, 600) << "a refused line leaves the field as it was";
}

// The harness build log's bookkeeping: start lines, distinct builders, frames from the first start line to completion.
TEST(AoCBuildSpeedTest, BuildLogCountsStartsBuildersAndFrames)
{
	AoCBuildLog log;
	Int builders = -1;
	UnsignedInt frames = 0;

	// one builder, uninterrupted: one start line, 300 frames counted from it, both ends included
	EXPECT_EQ(log.onBuildFrame(50, 10, 100, 0.0f), AoCBuildLog::START);
	for (UnsignedInt f = 51; f <= 349; ++f)
		EXPECT_EQ(log.onBuildFrame(f, 10, 100, 1.0f), AoCBuildLog::NOT_A_START) << f;
	EXPECT_TRUE(log.onComplete(349, 100, &builders, &frames));
	EXPECT_EQ(builders, 1);
	EXPECT_EQ(frames, 300u);
	EXPECT_FALSE(log.onComplete(350, 100, &builders, &frames)) << "a completed structure is forgotten";
	EXPECT_EQ(builders, 0);

	// a builder that leaves and comes back starts again; a second builder takes over; both are counted once
	EXPECT_EQ(log.onBuildFrame(400, 11, 101, 0.0f), AoCBuildLog::START);
	EXPECT_EQ(log.onBuildFrame(401, 11, 101, 0.5f), AoCBuildLog::NOT_A_START);
	EXPECT_EQ(log.onBuildFrame(450, 11, 101, 1.0f), AoCBuildLog::START) << "back after a gap";
	EXPECT_EQ(log.onBuildFrame(451, 11, 101, 1.5f), AoCBuildLog::NOT_A_START);
	EXPECT_EQ(log.onBuildFrame(451, 12, 101, 2.0f), AoCBuildLog::START) << "a second builder joins";
	EXPECT_EQ(log.onBuildFrame(452, 11, 101, 2.5f), AoCBuildLog::NOT_A_START) << "two builders on one frame";
	EXPECT_EQ(log.onBuildFrame(452, 12, 101, 3.0f), AoCBuildLog::NOT_A_START) << "two builders on one frame";
	EXPECT_TRUE(log.onComplete(500, 101, &builders, &frames));
	EXPECT_EQ(builders, 2);
	EXPECT_EQ(frames, 101u);

	// a structure with progress and no start line (a loaded game) is a resumed start; frames count from that line
	EXPECT_EQ(log.onBuildFrame(1802, 13, 102, 40.0f), AoCBuildLog::RESUMED);
	EXPECT_EQ(log.onBuildFrame(1803, 13, 102, 40.5f), AoCBuildLog::NOT_A_START);
	EXPECT_TRUE(log.onComplete(1900, 102, &builders, &frames));
	EXPECT_EQ(builders, 1);
	EXPECT_EQ(frames, 99u);

	// a frame earlier than the last one seen is a new game: everything is forgotten
	EXPECT_EQ(log.onBuildFrame(2000, 14, 103, 0.0f), AoCBuildLog::START);
	EXPECT_EQ(log.onBuildFrame(10, 14, 103, 0.0f), AoCBuildLog::START);
	EXPECT_TRUE(log.onComplete(19, 103, &builders, &frames));
	EXPECT_EQ(frames, 10u);
}

// Fixed storage: when every slot is taken, the structure worked at least recently makes room.
TEST(AoCBuildSpeedTest, BuildLogFullTableDropsTheLeastRecentlyWorked)
{
	AoCBuildLog log;
	for (UnsignedInt i = 0; i < (UnsignedInt)AOC_BUILD_LOG_OBJECTS; ++i)
		EXPECT_EQ(log.onBuildFrame(100 + i, 1000 + i, 1 + i, 0.0f), AoCBuildLog::START);
	// object 1 (worked at frame 100) is the least recent; object 2 is worked again
	EXPECT_EQ(log.onBuildFrame(300, 1001, 2, 5.0f), AoCBuildLog::START);
	EXPECT_EQ(log.onBuildFrame(301, 5000, 9999, 0.0f), AoCBuildLog::START);
	Int builders = 0;
	UnsignedInt frames = 0;
	EXPECT_FALSE(log.onComplete(302, 1, &builders, &frames)) << "the least recently worked structure made room";
	EXPECT_TRUE(log.onComplete(302, 2, &builders, &frames));
	EXPECT_EQ(frames, 302u - 101u + 1u);
	EXPECT_TRUE(log.onComplete(302, 9999, &builders, &frames));
	EXPECT_EQ(frames, 2u);
}
