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

// AoC F9: the pure parts of bounded wander (design D11 section 2.9): the module data defaults and parse table at both
// simulation rates, the point picker (inside the ring for every direction and many injected draws, a pure function of
// the draws), the pause range at 30 and 60 Hz, the in-flight look interval, the reach test, the runtime state's save
// round trip through the module's own save block and its CRC; and from the review of PR #9: ClearProducerFootprint,
// the update's transitions (walk, pause, walk; an interrupted stroll) and the watchdog.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <random>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "AoC/AoCSimRate.h"
#include "AoC/AoCWander.h"
#include "Common/GameCommon.h"
#include "Common/INI.h"
#include "Common/Snapshot.h"
#include "Common/XferCRC.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"

namespace
{

// Draws from a seeded std::mt19937: the injected random source for the many-draws tests.
class SeededRandom : public AoCWanderRandom
{
public:
	explicit SeededRandom( unsigned int seed ) : m_gen( seed ) {}
	virtual Int randomInt( Int lo, Int hi ) override { std::uniform_int_distribution<Int> d( lo, hi ); return d( m_gen ); }
	virtual Real randomReal( Real lo, Real hi ) override
	{
		if (lo >= hi)
			return hi;
		std::uniform_real_distribution<Real> d( lo, hi );
		return d( m_gen );
	}
private:
	std::mt19937 m_gen;
};

// Returns scripted values and records every call, to check which draws a pick makes and in what order.
class ScriptedRandom : public AoCWanderRandom
{
public:
	std::vector<Int> ints;
	std::vector<Real> reals;
	std::string calls;
	size_t nextInt = 0;
	size_t nextReal = 0;
	virtual Int randomInt( Int lo, Int hi ) override
	{
		calls += "i";
		Int v = nextInt < ints.size() ? ints[nextInt++] : lo;
		return v < lo ? lo : (v > hi ? hi : v);
	}
	virtual Real randomReal( Real lo, Real hi ) override
	{
		calls += "r";
		Real v = nextReal < reals.size() ? reals[nextReal++] : lo;
		return v < lo ? lo : (v > hi ? hi : v);
	}
};

// The module's save block through the module's own helper, AoCWanderState::xferBlock, which AoCWanderAIUpdate::xfer
// calls with AIUpdateInterface::xfer as the stock block; here the stock block is one Int standing in for it (review of
// PR #9, finding 5: the layout is tested through the code the module runs).
class StateHolder : public Snapshot
{
public:
	StateHolder() : stockBefore( 0 ) {}
	AoCWanderState state;
	Int stockBefore;
	static void stockBlock( Xfer *xfer, void *context ) { xfer->xferInt( &static_cast<StateHolder *>(context)->stockBefore ); }
protected:
	virtual void crc( Xfer *xfer ) override { state.crc(xfer); }
	virtual void xfer( Xfer *xfer ) override { state.xferBlock( xfer, stockBlock, this ); }
	virtual void loadPostProcess() override {}
};

// The same block written by hand in the documented order: the version byte, the stock block, then the eight fields.
class HandWrittenBlock : public Snapshot
{
public:
	HandWrittenBlock() : stockBefore( 0 ) {}
	AoCWanderState state;
	Int stockBefore;
protected:
	virtual void crc( Xfer *xfer ) override {}
	virtual void xfer( Xfer *xfer ) override
	{
		XferVersion version = 1;
		xfer->xferVersion( &version, 1 );
		xfer->xferInt( &stockBefore );
		xfer->xferBool( &state.anchored );
		xfer->xferCoord3D( &state.anchor );
		xfer->xferReal( &state.clearance );
		xfer->xferUnsignedInt( &state.nextMoveFrame );
		xfer->xferBool( &state.moving );
		xfer->xferCoord3D( &state.dest );
		xfer->xferCoord3D( &state.watchPos );
		xfer->xferUnsignedInt( &state.watchFrame );
	}
	virtual void loadPostProcess() override {}
};

Real distance2D( const Coord3D &a, const Coord3D &b )
{
	const double dx = (double)b.x - (double)a.x;
	const double dy = (double)b.y - (double)a.y;
	return (Real)sqrt(dx * dx + dy * dy);
}

class AoCWanderTest : public ::testing::Test
{
protected:
	void SetUp() override { m_startRate = AoCSimRate::g_framesPerSecond; }
	// The rate is fixed once startup has read it (AoCSimRate::lock) and the test process has locked it, so the tests set
	// the live value the conversions read directly, and put it back.
	void TearDown() override { AoCSimRate::g_framesPerSecond = m_startRate; }

	// Drives one row of the module's parse table the way the INI loader does: the key token is consumed, the rest of the
	// line is read with the static INI tokenizer.
	static void parseLine( AoCWanderData &data, const char *line )
	{
		char buffer[256];
		strncpy(buffer, line, sizeof(buffer) - 1);
		buffer[sizeof(buffer) - 1] = '\0';
		const char *key = ::strtok(buffer, INI::getSeps());
		ASSERT_NE(key, nullptr);
		for (const FieldParse *f = AoCWanderData::getFieldParse(); f->token != nullptr; ++f)
		{
			if (strcmp(f->token, key) == 0)
			{
				f->parse(nullptr, &data, (char *)&data + f->offset, f->userData);
				return;
			}
		}
		FAIL() << "no parse row for " << key;
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

TEST_F(AoCWanderTest, DefaultsAreTheDesignValuesAtBothRates)
{
	AoCSimRate::g_framesPerSecond = 30;
	AoCWanderData at30;
	EXPECT_FLOAT_EQ(at30.wanderRadius, 60.0f);
	EXPECT_EQ(at30.minPauseFrames, 60u) << "2000 ms at 30 Hz";
	EXPECT_EQ(at30.maxPauseFrames, 240u) << "8000 ms at 30 Hz";
	EXPECT_TRUE(at30.anchorToProducer);
	EXPECT_TRUE(at30.clearProducerFootprint);

	AoCSimRate::g_framesPerSecond = 60;
	AoCWanderData at60;
	EXPECT_EQ(at60.minPauseFrames, 120u) << "2000 ms at 60 Hz";
	EXPECT_EQ(at60.maxPauseFrames, 480u) << "8000 ms at 60 Hz";
}

TEST_F(AoCWanderTest, ParseTableReadsTheKeysWithTheIniDurationRule)
{
	AoCSimRate::g_framesPerSecond = 30;
	AoCWanderData data;
	parseLine(data, "WanderRadius = 45.5");
	parseLine(data, "MinPause = 1000");
	parseLine(data, "MaxPause = 3001");
	parseLine(data, "AnchorToProducer = No");
	parseLine(data, "ClearProducerFootprint = No");
	EXPECT_FLOAT_EQ(data.wanderRadius, 45.5f);
	EXPECT_EQ(data.minPauseFrames, 30u);
	EXPECT_EQ(data.maxPauseFrames, 91u) << "a partial frame rounds up, as INI::parseDurationUnsignedInt does";
	EXPECT_FALSE(data.anchorToProducer);
	EXPECT_FALSE(data.clearProducerFootprint);

	AoCSimRate::g_framesPerSecond = 60;
	AoCWanderData at60;
	parseLine(at60, "MinPause = 2000");
	parseLine(at60, "MaxPause = 8000");
	EXPECT_EQ(at60.minPauseFrames, 120u);
	EXPECT_EQ(at60.maxPauseFrames, 480u);
}

TEST_F(AoCWanderTest, DirectionsAreUnitVectorsEvenlyAroundTheCircle)
{
	Real x, y;
	AoCWander::direction(0, &x, &y);
	EXPECT_FLOAT_EQ(x, 1.0f); EXPECT_FLOAT_EQ(y, 0.0f);
	AoCWander::direction(16, &x, &y);
	EXPECT_FLOAT_EQ(x, 0.0f); EXPECT_FLOAT_EQ(y, 1.0f);
	AoCWander::direction(32, &x, &y);
	EXPECT_FLOAT_EQ(x, -1.0f); EXPECT_FLOAT_EQ(y, 0.0f);
	AoCWander::direction(48, &x, &y);
	EXPECT_FLOAT_EQ(x, 0.0f); EXPECT_FLOAT_EQ(y, -1.0f);

	const double step = 2.0 * 3.14159265358979323846 / (double)(Int)AOC_WANDER_DIRECTIONS;
	for (Int d = 0; d < AOC_WANDER_DIRECTIONS; ++d)
	{
		AoCWander::direction(d, &x, &y);
		EXPECT_NEAR(sqrt((double)x * x + (double)y * y), 1.0, 1e-6) << "direction " << d;
		EXPECT_NEAR(x, cos(d * step), 1e-6) << "direction " << d;
		EXPECT_NEAR(y, sin(d * step), 1e-6) << "direction " << d;
	}

	Real wx, wy;
	AoCWander::direction(AOC_WANDER_DIRECTIONS + 5, &wx, &wy);
	AoCWander::direction(5, &x, &y);
	EXPECT_EQ(wx, x); EXPECT_EQ(wy, y);
	AoCWander::direction(-1, &wx, &wy);
	AoCWander::direction(AOC_WANDER_DIRECTIONS - 1, &x, &y);
	EXPECT_EQ(wx, x); EXPECT_EQ(wy, y);
}

TEST_F(AoCWanderTest, PickerStaysInsideTheRingForEveryDirectionAndDraw)
{
	const Coord3D anchor = { 1234.5f, 678.25f, 12.0f };
	const Real radius = 60.0f;
	const Real clearances[] = { 0.0f, 92.19544f };
	const Real draws[] = { -5.0f, 0.0f, 0.001f, 17.3f, 30.0f, 59.999f, 60.0f, 75.0f, 1.0e6f };
	for (Real clearance : clearances)
	{
		for (Int d = 0; d < AOC_WANDER_DIRECTIONS; ++d)
		{
			for (Real draw : draws)
			{
				const Coord3D p = AoCWander::pickPoint(anchor, clearance, radius, d, draw);
				const Real dist = distance2D(anchor, p);
				EXPECT_LE(dist, clearance + radius + 1e-3f) << "direction " << d << " draw " << draw;
				EXPECT_GE(dist, clearance - 1e-3f) << "direction " << d << " draw " << draw;
				EXPECT_EQ(p.z, anchor.z);
				EXPECT_TRUE(AoCWander::withinReach(anchor, AoCWander::outerRadius(clearance, radius), 0.01f, p));
			}
		}
	}
}

TEST_F(AoCWanderTest, ManyInjectedDrawsStayInsideAndCoverTheRing)
{
	const Coord3D anchor = { 500.0f, 500.0f, 0.0f };
	const Real radius = 60.0f;
	const Real clearances[] = { 0.0f, 92.19544f };
	for (Real clearance : clearances)
	{
		SeededRandom random( 20261101u );
		const Real outer = AoCWander::outerRadius(clearance, radius);
		Real maxDist = 0.0f;
		Real minDist = 1.0e9f;
		Int quadrants[4] = { 0, 0, 0, 0 };
		for (Int n = 0; n < 100000; ++n)
		{
			const Coord3D p = AoCWander::drawPoint(anchor, clearance, radius, random);
			const Real dist = distance2D(anchor, p);
			ASSERT_LE(dist, outer + 1e-3f) << "draw " << n;
			ASSERT_GE(dist, clearance - 1e-3f) << "draw " << n;
			maxDist = dist > maxDist ? dist : maxDist;
			minDist = dist < minDist ? dist : minDist;
			quadrants[(p.x >= anchor.x ? 0 : 1) + (p.y >= anchor.y ? 0 : 2)]++;
		}
		EXPECT_GT(maxDist, outer - 0.5f) << "the outer edge is reached";
		EXPECT_LT(minDist, clearance + 0.5f) << "the inner edge is reached";
		for (Int q = 0; q < 4; ++q)
			EXPECT_GT(quadrants[q], 20000) << "quadrant " << q;
	}
}

TEST_F(AoCWanderTest, PickIsAPureFunctionOfTheTwoDraws)
{
	const Coord3D anchor = { 100.0f, 200.0f, 5.0f };
	ScriptedRandom script;
	script.ints.push_back(16);		// straight up (+y)
	script.reals.push_back(25.0f);
	const Coord3D p = AoCWander::drawPoint(anchor, 10.0f, 60.0f, script);
	EXPECT_EQ(script.calls, "ir") << "a direction, then a distance, nothing else";
	EXPECT_NEAR(p.x, 100.0f, 1e-4f);
	EXPECT_NEAR(p.y, 235.0f, 1e-4f) << "clearance 10 plus the distance 25";

	const Coord3D q = AoCWander::pickPoint(anchor, 10.0f, 60.0f, 16, 25.0f);
	EXPECT_EQ(memcmp(&p, &q, sizeof(Coord3D)), 0) << "the same draws give the same point, bit for bit";

	SeededRandom a( 7u ), b( 7u );
	for (Int n = 0; n < 1000; ++n)
	{
		const Coord3D pa = AoCWander::drawPoint(anchor, 0.0f, 60.0f, a);
		const Coord3D pb = AoCWander::drawPoint(anchor, 0.0f, 60.0f, b);
		ASSERT_EQ(memcmp(&pa, &pb, sizeof(Coord3D)), 0) << "draw " << n;
	}
}

TEST_F(AoCWanderTest, PausesLieInTheRangeAt30And60Hz)
{
	struct Case { Int rate; UnsignedInt lo; UnsignedInt hi; UnsignedInt look; };
	const Case cases[] = { { 30, 60u, 240u, 15u }, { 60, 120u, 480u, 30u } };
	for (const Case &c : cases)
	{
		AoCSimRate::g_framesPerSecond = c.rate;
		AoCWanderData data;	// MinPause 2000, MaxPause 8000
		EXPECT_EQ(data.minPauseFrames, c.lo) << c.rate << " Hz";
		EXPECT_EQ(data.maxPauseFrames, c.hi) << c.rate << " Hz";

		ScriptedRandom ends;
		ends.ints.push_back(-1000);	// clamped to the low end by the scripted source
		ends.ints.push_back(1000000);
		EXPECT_EQ(AoCWander::drawPause(data.minPauseFrames, data.maxPauseFrames, ends), c.lo);
		EXPECT_EQ(AoCWander::drawPause(data.minPauseFrames, data.maxPauseFrames, ends), c.hi);
		EXPECT_EQ(ends.calls, "ii") << "one draw per pause";

		SeededRandom random( 99u );
		UnsignedInt seenLo = 1000000u, seenHi = 0u;
		for (Int n = 0; n < 20000; ++n)
		{
			const UnsignedInt pause = AoCWander::drawPause(data.minPauseFrames, data.maxPauseFrames, random);
			ASSERT_GE(pause, c.lo);
			ASSERT_LE(pause, c.hi);
			seenLo = pause < seenLo ? pause : seenLo;
			seenHi = pause > seenHi ? pause : seenHi;
		}
		EXPECT_EQ(seenLo, c.lo);
		EXPECT_EQ(seenHi, c.hi);
		EXPECT_EQ(AoCWander::checkFrames(data.minPauseFrames), c.look) << "a quarter of MinPause while a move is in flight";
	}

	SeededRandom random( 3u );
	EXPECT_EQ(AoCWander::drawPause(90u, 30u, random), 90u) << "a maximum below the minimum is the minimum";
	EXPECT_EQ(AoCWander::drawPause(0u, 0u, random), 1u) << "never below one frame";
	EXPECT_EQ(AoCWander::checkFrames(3u), 1u);
	EXPECT_EQ(AoCWander::checkFrames(0u), 1u);
}

TEST_F(AoCWanderTest, ReachComparesSquaredDistancesWithTheSlack)
{
	const Coord3D anchor = { 0.0f, 0.0f, 0.0f };
	const Coord3D onEdge = { 60.0f, 0.0f, 0.0f };
	const Coord3D cellOut = { 0.0f, -69.5f, 0.0f };
	const Coord3D tooFar = { 50.0f, 50.0f, 0.0f };	// 70.7 from the anchor
	EXPECT_TRUE(AoCWander::withinReach(anchor, 60.0f, 0.0f, onEdge));
	EXPECT_FALSE(AoCWander::withinReach(anchor, 60.0f, 0.0f, cellOut));
	EXPECT_TRUE(AoCWander::withinReach(anchor, 60.0f, 10.0f, cellOut)) << "one pathfinder cell of slack";
	EXPECT_FALSE(AoCWander::withinReach(anchor, 60.0f, 10.0f, tooFar));
	EXPECT_EQ(AoCWander::distanceForLog(anchor, tooFar), 71);
	EXPECT_EQ(AoCWander::distanceForLog(anchor, onEdge), 60);
}

TEST_F(AoCWanderTest, StateRoundTripsThroughTheModulesSaveBlockAndIsInTheCrc)
{
	const std::string path = ::testing::TempDir() + "AoCWanderStateRoundTrip.sav";
	const std::string handPath = ::testing::TempDir() + "AoCWanderStateHand.sav";
	{
		StateHolder holder;
		holder.stockBefore = 4242;
		holder.state.anchored = TRUE;
		holder.state.anchor.set(1500.25f, 975.5f, 10.0f);
		holder.state.clearance = 92.19544f;
		holder.state.nextMoveFrame = 1937u;
		holder.state.moving = TRUE;
		holder.state.dest.set(1610.5f, 1001.0f, 10.0f);
		holder.state.watchPos.set(1550.75f, 980.25f, 10.0f);
		holder.state.watchFrame = 1900u;
		XferSave save;
		save.open(path.c_str());
		save.xferSnapshot(&holder);
		save.close();
	}
	{
		StateHolder loaded;
		XferLoad load;
		load.setOptions(XO_NO_POST_PROCESSING);
		load.open(path.c_str());
		load.xferSnapshot(&loaded);
		load.close();
		EXPECT_EQ(loaded.stockBefore, 4242) << "the stock AI block comes first and stays in step";
		EXPECT_TRUE(loaded.state.anchored);
		EXPECT_EQ(loaded.state.anchor.x, 1500.25f);
		EXPECT_EQ(loaded.state.anchor.y, 975.5f);
		EXPECT_EQ(loaded.state.anchor.z, 10.0f);
		EXPECT_EQ(loaded.state.clearance, 92.19544f);
		EXPECT_EQ(loaded.state.nextMoveFrame, 1937u);
		EXPECT_TRUE(loaded.state.moving);
		EXPECT_EQ(loaded.state.dest.x, 1610.5f);
		EXPECT_EQ(loaded.state.dest.y, 1001.0f);
		EXPECT_EQ(loaded.state.watchPos.x, 1550.75f);
		EXPECT_EQ(loaded.state.watchPos.y, 980.25f);
		EXPECT_EQ(loaded.state.watchFrame, 1900u);
	}
	// The layout: what xferBlock writes is byte for byte the documented order (version 1, stock block, eight fields).
	{
		HandWrittenBlock hand;
		hand.stockBefore = 4242;
		hand.state.anchored = TRUE;
		hand.state.anchor.set(1500.25f, 975.5f, 10.0f);
		hand.state.clearance = 92.19544f;
		hand.state.nextMoveFrame = 1937u;
		hand.state.moving = TRUE;
		hand.state.dest.set(1610.5f, 1001.0f, 10.0f);
		hand.state.watchPos.set(1550.75f, 980.25f, 10.0f);
		hand.state.watchFrame = 1900u;
		XferSave save;
		save.open(handPath.c_str());
		save.xferSnapshot(&hand);
		save.close();
		FILE *a = fopen(path.c_str(), "rb");
		FILE *b = fopen(handPath.c_str(), "rb");
		ASSERT_NE(a, nullptr);
		ASSERT_NE(b, nullptr);
		std::vector<char> da, db;
		for (int ch = fgetc(a); ch != EOF; ch = fgetc(a)) da.push_back((char)ch);
		for (int ch = fgetc(b); ch != EOF; ch = fgetc(b)) db.push_back((char)ch);
		fclose(a);
		fclose(b);
		EXPECT_FALSE(da.empty());
		EXPECT_EQ(da, db) << "xferBlock writes the version byte, the stock block, then the fields in the documented order";
	}
	remove(path.c_str());
	remove(handPath.c_str());

	StateHolder base;
	const UnsignedInt baseCrc = crcOf(base);
	StateHolder a, b, c, d, e, f, g, h;
	a.state.anchored = TRUE;
	b.state.anchor.x = 1.0f;
	c.state.clearance = 1.0f;
	d.state.nextMoveFrame = 1u;
	e.state.moving = TRUE;
	f.state.dest.y = 1.0f;
	g.state.watchPos.x = 1.0f;
	h.state.watchFrame = 1u;
	EXPECT_NE(crcOf(a), baseCrc) << "anchored";
	EXPECT_NE(crcOf(b), baseCrc) << "anchor";
	EXPECT_NE(crcOf(c), baseCrc) << "clearance";
	EXPECT_NE(crcOf(d), baseCrc) << "next move frame";
	EXPECT_NE(crcOf(e), baseCrc) << "move in flight";
	EXPECT_NE(crcOf(f), baseCrc) << "destination";
	EXPECT_NE(crcOf(g), baseCrc) << "watch position";
	EXPECT_NE(crcOf(h), baseCrc) << "watch frame";
}

// Review of PR #9, finding 2: ClearProducerFootprint. Anchored to a producer the clearance is its bounding radius unless
// the key is No; anchored to itself it is always 0.
TEST_F(AoCWanderTest, ClearanceFollowsClearProducerFootprint)
{
	EXPECT_FLOAT_EQ(AoCWander::anchorClearance(TRUE, TRUE, 92.19544f), 92.19544f);
	EXPECT_FLOAT_EQ(AoCWander::anchorClearance(TRUE, FALSE, 92.19544f), 0.0f) << "an ox pen: the ring sits on the centre";
	EXPECT_FLOAT_EQ(AoCWander::anchorClearance(FALSE, TRUE, 92.19544f), 0.0f) << "anchored to itself";
	EXPECT_FLOAT_EQ(AoCWander::anchorClearance(TRUE, TRUE, -1.0f), 0.0f);

	AoCWanderState s;
	const Coord3D centre = { 400.0f, 300.0f, 0.0f };
	const Coord3D spawn = { 460.0f, 335.0f, 0.0f };
	AoCWander::fixAnchor(s, 17u, centre, AoCWander::anchorClearance(TRUE, FALSE, 92.19544f), spawn);
	EXPECT_TRUE(s.anchored);
	EXPECT_EQ(s.anchor.x, 400.0f);
	EXPECT_EQ(s.clearance, 0.0f);
	EXPECT_EQ(s.nextMoveFrame, 17u) << "the first stroll is due at once";
	EXPECT_EQ(s.watchFrame, 17u);
	EXPECT_EQ(s.watchPos.x, 460.0f);
	EXPECT_FLOAT_EQ(AoCWander::outerRadius(s.clearance, 60.0f), 60.0f);
}

// Review of PR #9, finding 5: the update's transitions, through the pure steps the module calls in order.
TEST_F(AoCWanderTest, UpdateWalksThenPausesThenWalksAgain)
{
	AoCSimRate::g_framesPerSecond = 30;
	AoCWanderData data;	// 60 to 240 frames, look every 15
	AoCWanderState s;
	const Coord3D anchor = { 0.0f, 0.0f, 0.0f };
	const Coord3D here = { 100.0f, 0.0f, 0.0f };
	const Coord3D dest = { 0.0f, 120.0f, 0.0f };
	AoCWander::fixAnchor(s, 100u, anchor, 92.0f, here);

	EXPECT_FALSE(AoCWander::wantsMove(s, 100u, FALSE, FALSE)) << "the stock AI is busy (the exit path)";
	EXPECT_FALSE(AoCWander::wantsMove(s, 100u, TRUE, TRUE)) << "dead";
	EXPECT_EQ(AoCWander::sleepFrames(s, 100u, data.minPauseFrames), 15u) << "due but busy: look again in a quarter of MinPause";
	ASSERT_TRUE(AoCWander::wantsMove(s, 100u, TRUE, FALSE)) << "idle and due: walk";

	AoCWander::moveStarted(s, 100u, here, dest);
	EXPECT_TRUE(s.moving);
	EXPECT_EQ(s.dest.y, 120.0f);
	EXPECT_FALSE(AoCWander::wantsMove(s, 101u, TRUE, FALSE)) << "one stroll at a time";
	EXPECT_FALSE(AoCWander::moveEnded(s, FALSE)) << "still walking";
	EXPECT_EQ(AoCWander::sleepFrames(s, 101u, data.minPauseFrames), 15u) << "in flight: a quarter of MinPause";

	ASSERT_TRUE(AoCWander::moveEnded(s, TRUE)) << "the stock AI is idle again: the stroll has ended";
	AoCWander::pauseFor(s, 329u, 150u);
	EXPECT_FALSE(s.moving);
	EXPECT_EQ(s.nextMoveFrame, 479u);
	EXPECT_FALSE(AoCWander::moveEnded(s, TRUE)) << "nothing in flight";
	EXPECT_EQ(AoCWander::sleepFrames(s, 329u, data.minPauseFrames), 150u) << "pausing: sleep the rest of the pause";
	EXPECT_EQ(AoCWander::sleepFrames(s, 400u, data.minPauseFrames), 79u);
	EXPECT_FALSE(AoCWander::wantsMove(s, 478u, TRUE, FALSE));
	EXPECT_TRUE(AoCWander::wantsMove(s, 479u, TRUE, FALSE)) << "the pause is over";

	// No usable draw: a pause instead of a stroll, then due again.
	AoCWander::pauseFor(s, 479u, 60u);
	EXPECT_FALSE(s.moving);
	EXPECT_TRUE(AoCWander::wantsMove(s, 539u, TRUE, FALSE));
}

TEST_F(AoCWanderTest, AnInterruptedStrollEndsWhenTheStockAIIsIdleAgain)
{
	AoCWanderState s;
	const Coord3D anchor = { 0.0f, 0.0f, 0.0f };
	Coord3D pos = { 100.0f, 0.0f, 0.0f };
	AoCWander::fixAnchor(s, 0u, anchor, 92.0f, pos);
	AoCWander::moveStarted(s, 10u, pos, anchor);
	// Panic (AI_MOVE_AWAY_FROM_REPULSORS) or a push (AI_MOVE_OUT_OF_THE_WAY): not the stock move the watchdog watches,
	// whatever the walker does, for longer than any MaxPause.
	for (UnsignedInt frame = 11u; frame < 2000u; ++frame)
	{
		ASSERT_FALSE(AoCWander::watchStuck(s, frame, FALSE, pos, 240u)) << "frame " << frame;
		ASSERT_FALSE(AoCWander::moveEnded(s, FALSE));
	}
	EXPECT_TRUE(s.moving) << "the stroll counts as in flight until the stock AI is idle";
	EXPECT_TRUE(AoCWander::moveEnded(s, TRUE)) << "then it ends, and a pause follows";
}

TEST_F(AoCWanderTest, WatchdogTakesOverAWalkerLockedInAStockMove)
{
	struct Case { Int rate; UnsignedInt stuck; };
	const Case cases[] = { { 30, 240u }, { 60, 480u } };
	for (const Case &c : cases)
	{
		AoCSimRate::g_framesPerSecond = c.rate;
		AoCWanderData data;
		ASSERT_EQ(data.maxPauseFrames, c.stuck) << "MaxPause is the watchdog's patience";

		// A stroll in flight that stops on one spot in AI_MOVE_TO (two units locked together).
		AoCWanderState s;
		const Coord3D anchor = { 0.0f, 0.0f, 0.0f };
		const Coord3D locked = { 4205.0f, 1275.0f, 0.0f };
		AoCWander::fixAnchor(s, 1000u, anchor, 92.0f, locked);
		AoCWander::moveStarted(s, 1000u, locked, anchor);
		EXPECT_FALSE(AoCWander::watchStuck(s, 1000u + c.stuck - 1u, TRUE, locked, data.maxPauseFrames)) << c.rate << " Hz";
		ASSERT_TRUE(AoCWander::watchStuck(s, 1000u + c.stuck, TRUE, locked, data.maxPauseFrames)) << c.rate << " Hz";
		EXPECT_FALSE(s.moving) << "ready for a fresh pick";
		EXPECT_EQ(s.nextMoveFrame, 1000u + c.stuck) << "at once";
		EXPECT_EQ(s.watchFrame, 1000u + c.stuck) << "the watch restarts";
		EXPECT_TRUE(AoCWander::wantsMove(s, 1000u + c.stuck, TRUE, FALSE)) << "once the module has sent the stock AI idle";
	}

	AoCSimRate::g_framesPerSecond = 30;
	const Coord3D anchor = { 0.0f, 0.0f, 0.0f };

	// Progress restarts the watch: 1.1 units counts, 0.9 does not.
	{
		AoCWanderState s;
		Coord3D pos = { 100.0f, 0.0f, 0.0f };
		AoCWander::fixAnchor(s, 0u, anchor, 92.0f, pos);
		AoCWander::moveStarted(s, 0u, pos, anchor);
		pos.x += 0.9f;
		EXPECT_FALSE(AoCWander::watchStuck(s, 100u, TRUE, pos, 240u));
		EXPECT_EQ(s.watchFrame, 0u) << "0.9 units is no progress";
		pos.x += 0.2f;
		EXPECT_FALSE(AoCWander::watchStuck(s, 200u, TRUE, pos, 240u));
		EXPECT_EQ(s.watchFrame, 200u) << "1.1 units since the watch began is progress";
		EXPECT_FALSE(AoCWander::watchStuck(s, 439u, TRUE, pos, 240u));
		EXPECT_TRUE(AoCWander::watchStuck(s, 440u, TRUE, pos, 240u)) << "240 frames after the last progress";
	}

	// A walker that walks normally is never stuck.
	{
		AoCWanderState s;
		Coord3D pos = { 100.0f, 0.0f, 0.0f };
		AoCWander::fixAnchor(s, 0u, anchor, 92.0f, pos);
		AoCWander::moveStarted(s, 0u, pos, anchor);
		for (UnsignedInt frame = 1u; frame < 1000u; ++frame)
		{
			pos.y += 0.67f;		// 20 units a second at 30 Hz
			ASSERT_FALSE(AoCWander::watchStuck(s, frame, TRUE, pos, 240u)) << "frame " << frame;
		}
	}

	// Pausing: a stock move then (the stock collision code separating two walkers) is left alone until the pause is
	// over; then a walker that has been locked since is taken over at once. This is the walker the review saw lost
	// mid-run (171 and 172), and the pairs spawned on one spot (nothing in flight, due at once, locked from the start).
	{
		AoCWanderState s;
		const Coord3D pos = { 100.0f, 0.0f, 0.0f };
		AoCWander::fixAnchor(s, 0u, anchor, 92.0f, pos);
		AoCWander::pauseFor(s, 3003u, 200u);	// due at 3203
		EXPECT_FALSE(AoCWander::watchStuck(s, 3003u, FALSE, pos, 240u)) << "idle at the end of the stroll: the watch restarts";
		for (UnsignedInt frame = 3004u; frame < 3203u; ++frame)
			ASSERT_FALSE(AoCWander::watchStuck(s, frame, TRUE, pos, 240u)) << "pausing, frame " << frame;
		EXPECT_FALSE(AoCWander::watchStuck(s, 3242u, TRUE, pos, 240u)) << "the pause is over, but 239 frames only";
		EXPECT_TRUE(AoCWander::watchStuck(s, 3243u, TRUE, pos, 240u)) << "locked for MaxPause: taken over";

		AoCWanderState spawned;
		AoCWander::fixAnchor(spawned, 17u, anchor, 92.0f, pos);	// due at once, the stock move from the spawn locked
		EXPECT_FALSE(AoCWander::watchStuck(spawned, 18u, TRUE, pos, 240u));
		EXPECT_TRUE(AoCWander::watchStuck(spawned, 257u, TRUE, pos, 240u)) << "locked since the spawn: taken over after MaxPause";
	}
}

} // namespace
