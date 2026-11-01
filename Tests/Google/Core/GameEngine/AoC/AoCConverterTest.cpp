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

// AoC F3: the pure parts of the converter module (design D11 section 2.3): the cycle arithmetic at 30 and 60 Hz, the INI
// fields and their errors, the input check and the idle reasons, the passive producer, the restart after construction,
// and the save round trip and CRC of the runtime fields.

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "AoC/AoCConverterUpdate.h"
#include "AoC/AoCCost.h"
#include "AoC/AoCGather.h"
#include "AoC/AoCResourceBank.h"
#include "AoC/AoCRules.h"
#include "AoC/AoCSimRate.h"
#include "Common/GameCommon.h"
#include "Common/INI.h"
#include "Common/ModelState.h"
#include "Common/Snapshot.h"
#include "Common/XferCRC.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"

namespace
{

enum { CASH = AOC_RESOURCE_CASH, FOOD = 1, WOOD = 2, IRON = 3, STEEL = 4 };

// The runtime fields through the very calls the module makes: AoCConverterUpdate::xfer writes its version byte
// (AoCConverterState::XFER_VERSION) and the UpdateModule block, then calls AoCConverterState::xfer with that version;
// its crc calls AoCConverterState::crc. The holder calls only the state's own helpers, with no stand-in fields, so the
// bytes it writes are exactly the state's part of the module's block (review F3, finding 4).
class StateHolder : public Snapshot
{
public:
	AoCConverterState state;
	XferVersion version;	///< the block version the module would have read
	StateHolder() : version( AoCConverterState::XFER_VERSION ) {}
protected:
	virtual void crc( Xfer *xfer ) override { state.crc(xfer); }
	virtual void xfer( Xfer *xfer ) override { state.xfer( xfer, version ); }
	virtual void loadPostProcess() override {}
};

std::vector<unsigned char> readFile( const std::string &path )
{
	std::vector<unsigned char> bytes;
	FILE *f = fopen(path.c_str(), "rb");
	if (f == nullptr)
		return bytes;
	int c;
	while ((c = fgetc(f)) != EOF)
		bytes.push_back((unsigned char)c);
	fclose(f);
	return bytes;
}

class AoCConverterTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_rules = new AoCRules;
		m_rules->init();
		TheAoCRules = m_rules;
		m_startRate = AoCSimRate::g_framesPerSecond;
	}

	void TearDown() override
	{
		AoCSimRate::g_framesPerSecond = m_startRate;
		TheAoCRules = nullptr;
		delete m_rules;
	}

	void defineStandardTypes()
	{
		m_rules->defineType("Food", "", "", 1000, FALSE, -1);
		m_rules->defineType("Wood", "", "", 1000, FALSE, -1);
		m_rules->defineType("Iron", "", "", 1500, FALSE, -1);
		m_rules->defineType("Steel", "", "", 4000, FALSE, -1);
	}

	// Drives a FieldParse procedure the way the INI loader does: the key token is consumed, the rest of the line is read
	// with the static INI tokenizer.
	static void startLine( char *buffer, size_t size, const char *line )
	{
		strncpy(buffer, line, size - 1);
		buffer[size - 1] = '\0';
		::strtok(buffer, INI::getSeps());
	}

	typedef void (*FieldProc)( INI *, void *, void *, const void * );
	static void parse( FieldProc proc, AoCConverterRecipe &recipe, const char *line )
	{
		char buffer[256];
		startLine(buffer, sizeof(buffer), line);
		proc(nullptr, nullptr, &recipe, nullptr);
	}

	// The mill of D2: 2 iron to 1 steel every 4 s, at the live rate.
	static AoCConverterRecipe mill( Bool requiresPower = FALSE )
	{
		AoCConverterRecipe r;
		parse(AoCConverterRecipe::parseInputType, r, "InputType = Iron");
		parse(AoCConverterRecipe::parseInputAmount, r, "InputAmount = 2");
		parse(AoCConverterRecipe::parseOutputType, r, "OutputType = Steel");
		parse(AoCConverterRecipe::parseOutputAmount, r, "OutputAmount = 1");
		parse(AoCConverterRecipe::parseCycleTime, r, "CycleTime = 4000");
		r.requiresPower = requiresPower;
		r.finish();
		return r;
	}

	// The livestock farm of D2 2.4: 1 food every 5 s, no input.
	static AoCConverterRecipe farm()
	{
		AoCConverterRecipe r;
		parse(AoCConverterRecipe::parseOutputType, r, "OutputType = Food");
		parse(AoCConverterRecipe::parseOutputAmount, r, "OutputAmount = 1");
		parse(AoCConverterRecipe::parseCycleTime, r, "CycleTime = 5000");
		r.finish();
		return r;
	}

	// Runs the module's loop for one recipe from frame 'created' up to and including frame 'until': the first cycle
	// ends CycleTime after creation, each cycle end sleeps what endCycle says. Returns the number of conversions.
	static Int runUntil( AoCConverterState &state, const AoCConverterRecipe &recipe, const AoCConverterFacts &facts,
		AoCResourceBank &bank, UnsignedInt until )
	{
		Int ran = 0;
		while (state.cycleEndFrame <= until)
		{
			const AoCConverterStep step = AoCConverter::endCycle(state, recipe, facts, &bank, state.cycleEndFrame);
			if (step.ran)
				++ran;
		}
		return ran;
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

	AoCRules *m_rules;
	Int m_startRate;
};

}	// namespace

// D11 F15 rate independence: a cycle of 4000 ms is 120 frames at 30 Hz and 240 at 60 Hz, the one-second retry 30 and
// 60, through the F15 helper (AoCGather::msecsToFrames), as F2 converts its gather delays.
TEST_F(AoCConverterTest, CycleArithmeticFollowsTheRate)
{
	defineStandardTypes();
	// The rate is fixed once startup has read it (AoCSimRate::lock), and the test process has locked it, so the test
	// sets the live value the conversion reads directly and TearDown puts it back.
	AoCSimRate::g_framesPerSecond = 30;
	AoCConverterRecipe at30 = mill();
	EXPECT_EQ(at30.cycleMsecs, 4000u);
	EXPECT_EQ(at30.cycleFrames, 120u);
	EXPECT_EQ(AoCConverter::retryFrames(), 30u);
	EXPECT_EQ(farm().cycleFrames, 150u);

	AoCSimRate::g_framesPerSecond = 60;
	AoCConverterRecipe at60 = mill();
	EXPECT_EQ(at60.cycleFrames, 240u);
	EXPECT_EQ(AoCConverter::retryFrames(), 60u);
	EXPECT_EQ(farm().cycleFrames, 300u);

	// A cycle end sleeps CycleTime when it runs and one second when it does not, and records the next cycle end.
	AoCResourceBank bank;
	bank.init(nullptr);
	bank.deposit(IRON, 2);
	AoCConverterState state;
	AoCConverterFacts facts;
	AoCConverterStep step = AoCConverter::endCycle(state, at60, facts, &bank, 240);
	EXPECT_TRUE(step.ran);
	EXPECT_EQ(step.sleepFrames, 240u);
	EXPECT_EQ(state.cycleEndFrame, 480u);
	step = AoCConverter::endCycle(state, at60, facts, &bank, 480);
	EXPECT_FALSE(step.ran);
	EXPECT_EQ(step.sleepFrames, 60u) << "the retry is one second at 60 Hz";
	EXPECT_EQ(state.cycleEndFrame, 540u);

	// 1 ms is still a whole frame (the INI duration rule rounds up), never 0.
	AoCSimRate::g_framesPerSecond = 30;
	AoCConverterRecipe fast;
	parse(AoCConverterRecipe::parseCycleTime, fast, "CycleTime = 1");
	EXPECT_EQ(fast.cycleFrames, 1u);
}

TEST_F(AoCConverterTest, FieldsParseIntoTwoCostVectors)
{
	defineStandardTypes();
	AoCConverterRecipe r;
	EXPECT_TRUE(r.isPassive()) << "no InputType: a passive producer";
	EXPECT_EQ(r.inputAmount, 1u) << "amounts default to 1";
	EXPECT_EQ(r.outputAmount, 1u);
	EXPECT_FALSE(r.requiresPower) << "RequiresPower defaults to No";
	EXPECT_EQ(r.runningCondition, -1) << "RunningCondition defaults to none";

	parse(AoCConverterRecipe::parseInputType, r, "InputType = Iron");
	parse(AoCConverterRecipe::parseInputAmount, r, "InputAmount = 3");
	parse(AoCConverterRecipe::parseOutputType, r, "OutputType = Steel");
	parse(AoCConverterRecipe::parseOutputAmount, r, "OutputAmount = 2");
	parse(AoCConverterRecipe::parseCycleTime, r, "CycleTime = 4000");
	parse(AoCConverterRecipe::parseRequiresPower, r, "RequiresPower = Yes");
	parse(AoCConverterRecipe::parseRunningCondition, r, "RunningCondition = DAMAGED");
	r.finish();
	EXPECT_FALSE(r.isPassive());
	EXPECT_EQ(r.inputType, IRON);
	EXPECT_EQ(r.outputType, STEEL);
	EXPECT_TRUE(r.requiresPower);
	EXPECT_EQ(r.runningCondition, ModelConditionFlags::getSingleBitFromName("DAMAGED"));
	AoCCost in, out;
	in.amount[IRON] = 3;	// China's 3 to 2 ratio is data (D11 2.3)
	out.amount[STEEL] = 2;
	EXPECT_TRUE(r.input == in) << "the input vector the bank withdraws as it is";
	EXPECT_TRUE(r.output == out) << "the output vector the bank deposits as it is";

#if RTS_ZEROHOUR
	parse(AoCConverterRecipe::parseRunningCondition, r, "RunningCondition = USER_1");
	EXPECT_EQ(r.runningCondition, (Int)MODELCONDITION_USER_1);
#endif
	parse(AoCConverterRecipe::parseRunningCondition, r, "RunningCondition = None");
	EXPECT_EQ(r.runningCondition, -1);

	// A passive producer: InputAmount is ignored and the input vector is zero.
	AoCConverterRecipe p;
	parse(AoCConverterRecipe::parseInputAmount, p, "InputAmount = 5");
	parse(AoCConverterRecipe::parseOutputType, p, "OutputType = Food");
	parse(AoCConverterRecipe::parseCycleTime, p, "CycleTime = 5000");
	p.finish();
	EXPECT_TRUE(p.isPassive());
	EXPECT_TRUE(p.input.isZero());
	EXPECT_EQ(p.output.amount[FOOD], 1u);
}

// Decision 1: an unknown type name, an amount below 1, a CycleTime below 1 ms, a missing OutputType or CycleTime, an
// unknown condition and the module without resource types are INI errors (the loader names the file, line and field).
TEST_F(AoCConverterTest, BadFieldsAreIniErrors)
{
	EXPECT_ANY_THROW(AoCConverterRecipe::requireResourceTypes()) << "stock data: no AoCResourceType defined";
	defineStandardTypes();
	EXPECT_NO_THROW(AoCConverterRecipe::requireResourceTypes());

	AoCConverterRecipe r;
	EXPECT_ANY_THROW(parse(AoCConverterRecipe::parseInputType, r, "InputType = Gold"));
	EXPECT_ANY_THROW(parse(AoCConverterRecipe::parseOutputType, r, "OutputType = Gold"));
	EXPECT_ANY_THROW(parse(AoCConverterRecipe::parseInputAmount, r, "InputAmount = 0"));
	EXPECT_ANY_THROW(parse(AoCConverterRecipe::parseInputAmount, r, "InputAmount = -2"));
	EXPECT_ANY_THROW(parse(AoCConverterRecipe::parseOutputAmount, r, "OutputAmount = 0"));
	EXPECT_ANY_THROW(parse(AoCConverterRecipe::parseCycleTime, r, "CycleTime = 0"));
	EXPECT_ANY_THROW(parse(AoCConverterRecipe::parseCycleTime, r, "CycleTime = -4000"));
	EXPECT_ANY_THROW(parse(AoCConverterRecipe::parseRunningCondition, r, "RunningCondition = NOT_A_CONDITION"));

	AoCConverterRecipe noOutput;
	parse(AoCConverterRecipe::parseInputType, noOutput, "InputType = Iron");
	parse(AoCConverterRecipe::parseCycleTime, noOutput, "CycleTime = 4000");
	EXPECT_ANY_THROW(noOutput.finish()) << "OutputType is required";

	AoCConverterRecipe noCycle;
	parse(AoCConverterRecipe::parseOutputType, noCycle, "OutputType = Steel");
	EXPECT_ANY_THROW(noCycle.finish()) << "CycleTime is required";
}

// The input check: a cycle end converts only when the bank holds the whole input, withdraws exactly the input and
// deposits exactly the output; without it the bank is untouched and the reason is "no input", logged once.
TEST_F(AoCConverterTest, ConvertsWhileTheBankHoldsTheInput)
{
	defineStandardTypes();
	AoCSimRate::g_framesPerSecond = 30;
	const AoCConverterRecipe r = mill();
	AoCResourceBank bank;
	bank.init(nullptr);
	bank.deposit(IRON, 5);
	AoCConverterState state;
	AoCConverterFacts facts;

	AoCConverterStep step = AoCConverter::endCycle(state, r, facts, &bank, 120);
	EXPECT_TRUE(step.ran);
	EXPECT_FALSE(step.logIdle);
	EXPECT_EQ(bank.count(IRON), 3u);
	EXPECT_EQ(bank.count(STEEL), 1u);
	EXPECT_TRUE(state.running);
	EXPECT_EQ(state.idleReason, (Int)AOC_CONVERTER_IDLE_NONE);
	EXPECT_EQ(state.cycleEndFrame, 240u);

	step = AoCConverter::endCycle(state, r, facts, &bank, 240);
	EXPECT_TRUE(step.ran);
	EXPECT_EQ(bank.count(IRON), 1u);
	EXPECT_EQ(bank.count(STEEL), 2u);

	step = AoCConverter::endCycle(state, r, facts, &bank, 360);
	EXPECT_FALSE(step.ran) << "1 iron is not the input of 2";
	EXPECT_TRUE(step.logIdle) << "running to idle: one idle line";
	EXPECT_EQ(bank.count(IRON), 1u) << "nothing is withdrawn when the input is short";
	EXPECT_EQ(bank.count(STEEL), 2u);
	EXPECT_FALSE(state.running);
	EXPECT_EQ(state.idleReason, (Int)AOC_CONVERTER_IDLE_NO_INPUT);
	EXPECT_EQ(state.cycleEndFrame, 390u) << "retry in one second";

	step = AoCConverter::endCycle(state, r, facts, &bank, 390);
	EXPECT_FALSE(step.ran);
	EXPECT_FALSE(step.logIdle) << "the same reason again: no second idle line";

	bank.deposit(IRON, 1);
	step = AoCConverter::endCycle(state, r, facts, &bank, 420);
	EXPECT_TRUE(step.ran) << "exactly the input: converts";
	EXPECT_EQ(bank.count(IRON), 0u);
	EXPECT_EQ(bank.count(STEEL), 3u);
	EXPECT_EQ(state.cycleEndFrame, 540u);

	// 100 iron at the start (the test overlay): 50 cycles of 120 frames, the last at frame 6000, then idle.
	AoCResourceBank hundred;
	hundred.init(nullptr);
	hundred.deposit(IRON, 100);
	AoCConverterState s;
	s.cycleEndFrame = r.cycleFrames;
	EXPECT_EQ(runUntil(s, r, facts, hundred, 6000), 50);
	EXPECT_EQ(hundred.count(IRON), 0u);
	EXPECT_EQ(hundred.count(STEEL), 50u);
	EXPECT_EQ(s.cycleEndFrame, 6120u);
	step = AoCConverter::endCycle(s, r, facts, &hundred, 6120);
	EXPECT_FALSE(step.ran);
	EXPECT_TRUE(step.logIdle);
	EXPECT_EQ(s.idleReason, (Int)AOC_CONVERTER_IDLE_NO_INPUT);
}

// The idle reasons and their order; RequiresPower decides whether underpowered matters; every reason change logs once.
TEST_F(AoCConverterTest, IdleReasons)
{
	defineStandardTypes();
	AoCSimRate::g_framesPerSecond = 30;
	const AoCConverterRecipe powered = mill(TRUE);
	const AoCConverterRecipe unpowered = mill(FALSE);

	AoCConverterFacts f;
	EXPECT_EQ(AoCConverter::blockedReason(powered, f), AOC_CONVERTER_IDLE_NONE);
	f.underpowered = TRUE;
	EXPECT_EQ(AoCConverter::blockedReason(powered, f), AOC_CONVERTER_IDLE_NO_POWER);
	EXPECT_EQ(AoCConverter::blockedReason(unpowered, f), AOC_CONVERTER_IDLE_NONE) << "RequiresPower = No ignores a brownout";
	f.neutral = TRUE;
	EXPECT_EQ(AoCConverter::blockedReason(powered, f), AOC_CONVERTER_IDLE_NEUTRAL);
	f.sold = TRUE;
	EXPECT_EQ(AoCConverter::blockedReason(powered, f), AOC_CONVERTER_IDLE_SOLD);
	f.underConstruction = TRUE;
	EXPECT_EQ(AoCConverter::blockedReason(powered, f), AOC_CONVERTER_IDLE_UNDER_CONSTRUCTION);

	EXPECT_STREQ(AoCConverter::reasonName(AOC_CONVERTER_IDLE_NO_INPUT), "no-input");
	EXPECT_STREQ(AoCConverter::reasonName(AOC_CONVERTER_IDLE_NO_POWER), "no-power");
	EXPECT_STREQ(AoCConverter::reasonName(AOC_CONVERTER_IDLE_UNDER_CONSTRUCTION), "under-construction");
	EXPECT_STREQ(AoCConverter::reasonName(AOC_CONVERTER_IDLE_SOLD), "sold");
	EXPECT_STREQ(AoCConverter::reasonName(AOC_CONVERTER_IDLE_NEUTRAL), "neutral");

	// Through endCycle: a blocked object never touches the bank; each change of reason gives one idle line.
	AoCResourceBank bank;
	bank.init(nullptr);
	bank.deposit(IRON, 10);
	AoCConverterState state;
	AoCConverterFacts brownout;
	brownout.underpowered = TRUE;
	AoCConverterStep step = AoCConverter::endCycle(state, powered, brownout, &bank, 120);
	EXPECT_FALSE(step.ran);
	EXPECT_TRUE(step.logIdle);
	EXPECT_EQ(state.idleReason, (Int)AOC_CONVERTER_IDLE_NO_POWER);
	EXPECT_EQ(bank.count(IRON), 10u);
	step = AoCConverter::endCycle(state, powered, brownout, &bank, 150);
	EXPECT_FALSE(step.logIdle) << "still no power: no second line";
	AoCConverterFacts selling;
	selling.sold = TRUE;
	selling.underpowered = TRUE;
	step = AoCConverter::endCycle(state, powered, selling, &bank, 180);
	EXPECT_TRUE(step.logIdle) << "no power to sold: a new line";
	EXPECT_EQ(state.idleReason, (Int)AOC_CONVERTER_IDLE_SOLD);
	EXPECT_EQ(bank.count(IRON), 10u);

	// The power plant stands: the next retry converts.
	AoCConverterFacts ok;
	step = AoCConverter::endCycle(state, powered, ok, &bank, 210);
	EXPECT_TRUE(step.ran);
	EXPECT_EQ(bank.count(IRON), 8u);
	// The same converter keeps running in a brownout when it does not need power.
	step = AoCConverter::endCycle(state, unpowered, brownout, &bank, 330);
	EXPECT_TRUE(step.ran);

	// No owner at all counts as neutral.
	AoCConverterState orphan;
	step = AoCConverter::endCycle(orphan, unpowered, ok, nullptr, 120);
	EXPECT_FALSE(step.ran);
	EXPECT_EQ(orphan.idleReason, (Int)AOC_CONVERTER_IDLE_NEUTRAL);
}

// D11 F3 decision 3: a cycle that ends while the object is under construction retries every second and converts at the
// first retry once the object is ready, then keeps whole cycles; nothing is caught up for the time spent waiting.
TEST_F(AoCConverterTest, RestartsCleanlyAfterConstruction)
{
	defineStandardTypes();
	AoCSimRate::g_framesPerSecond = 30;
	const AoCConverterRecipe r = mill();
	AoCResourceBank bank;
	bank.init(nullptr);
	bank.deposit(IRON, 100);
	AoCConverterState state;
	state.cycleEndFrame = 120;	// placed at frame 0
	AoCConverterFacts building;
	building.underConstruction = TRUE;

	Int idleLines = 0;
	while (state.cycleEndFrame <= 900)	// construction ends at frame 900
	{
		const AoCConverterStep step = AoCConverter::endCycle(state, r, building, &bank, state.cycleEndFrame);
		EXPECT_FALSE(step.ran);
		idleLines += step.logIdle ? 1 : 0;
	}
	EXPECT_EQ(idleLines, 1) << "one under-construction line for the whole build";
	EXPECT_EQ(state.idleReason, (Int)AOC_CONVERTER_IDLE_UNDER_CONSTRUCTION);
	EXPECT_EQ(state.cycleEndFrame, 930u) << "retries every 30 frames from frame 120";
	EXPECT_EQ(bank.count(IRON), 100u);

	AoCConverterFacts ready;
	AoCConverterStep step = AoCConverter::endCycle(state, r, ready, &bank, state.cycleEndFrame);
	EXPECT_TRUE(step.ran) << "the first retry after completion converts";
	EXPECT_EQ(bank.count(IRON), 98u) << "one cycle's input, no catch-up";
	EXPECT_EQ(state.cycleEndFrame, 1050u) << "then a whole cycle";
}

// D2 2.4: the livestock farm, food from nothing, 12 per minute per farm at either rate.
TEST_F(AoCConverterTest, PassiveProducerGivesTwelveFoodPerMinute)
{
	defineStandardTypes();
	const Int rates[2] = { 30, 60 };
	for (Int n = 0; n < 2; ++n)
	{
		AoCSimRate::g_framesPerSecond = rates[n];
		const AoCConverterRecipe r = farm();
		AoCResourceBank bank;
		bank.init(nullptr);
		AoCConverterState state;
		state.cycleEndFrame = r.cycleFrames;	// created at frame 0
		AoCConverterFacts facts;
		const UnsignedInt minute = (UnsignedInt)(60 * rates[n]);
		EXPECT_EQ(runUntil(state, r, facts, bank, minute), 12) << "at " << rates[n] << " Hz";
		EXPECT_EQ(bank.count(FOOD), 12u);
		EXPECT_TRUE(state.running);
		EXPECT_EQ(runUntil(state, r, facts, bank, 5 * minute), 48) << "60 per five minutes";
		EXPECT_EQ(bank.count(FOOD), 60u);
		EXPECT_EQ(bank.count(CASH), 0u) << "nothing else moves";
		EXPECT_EQ(bank.count(IRON), 0u);
	}

	// A passive producer still respects the object: no food while neutral (under construction: the next test).
	AoCSimRate::g_framesPerSecond = 30;
	const AoCConverterRecipe r = farm();
	AoCResourceBank bank;
	bank.init(nullptr);
	AoCConverterState state;
	AoCConverterFacts neutral;
	neutral.neutral = TRUE;
	EXPECT_FALSE(AoCConverter::endCycle(state, r, neutral, &bank, 150).ran);
	EXPECT_EQ(bank.count(FOOD), 0u);
}

// Review F3, finding 4: a farm being built gives no food, one idle line for the whole build, then food from the first
// retry after completion.
TEST_F(AoCConverterTest, PassiveProducerWaitsForConstruction)
{
	defineStandardTypes();
	AoCSimRate::g_framesPerSecond = 30;
	const AoCConverterRecipe r = farm();
	AoCResourceBank bank;
	bank.init(nullptr);
	AoCConverterState state;
	state.cycleEndFrame = r.cycleFrames;	// placed at frame 0, first cycle end at 150
	AoCConverterFacts building;
	building.underConstruction = TRUE;
	Int idleLines = 0;
	while (state.cycleEndFrame <= 600)	// construction ends at frame 600
	{
		const AoCConverterStep step = AoCConverter::endCycle(state, r, building, &bank, state.cycleEndFrame);
		EXPECT_FALSE(step.ran);
		idleLines += step.logIdle ? 1 : 0;
	}
	EXPECT_EQ(idleLines, 1);
	EXPECT_EQ(state.idleReason, (Int)AOC_CONVERTER_IDLE_UNDER_CONSTRUCTION);
	EXPECT_FALSE(state.running);
	EXPECT_EQ(bank.count(FOOD), 0u) << "no food while under construction";
	EXPECT_EQ(state.cycleEndFrame, 630u);

	AoCConverterFacts ready;
	EXPECT_TRUE(AoCConverter::endCycle(state, r, ready, &bank, state.cycleEndFrame).ran);
	EXPECT_EQ(bank.count(FOOD), 1u);
	EXPECT_EQ(state.cycleEndFrame, 780u) << "then whole cycles of 150 frames";
}

// Review F3, finding 3: from creation until its first cycle end a converter reports STARTING (running FALSE), and the
// first cycle end replaces it with what happened, logging an idle line only when it idles.
TEST_F(AoCConverterTest, StartingUntilTheFirstCycleEnd)
{
	defineStandardTypes();
	AoCSimRate::g_framesPerSecond = 30;
	const AoCConverterRecipe r = mill();
	AoCConverterState fresh;
	EXPECT_FALSE(fresh.running);
	EXPECT_EQ(fresh.idleReason, (Int)AOC_CONVERTER_IDLE_STARTING);
	EXPECT_STREQ(AoCConverter::reasonName(AOC_CONVERTER_IDLE_STARTING), "starting");

	AoCResourceBank empty;
	empty.init(nullptr);
	AoCConverterFacts facts;
	AoCConverterState a;
	AoCConverterStep step = AoCConverter::endCycle(a, r, facts, &empty, 120);
	EXPECT_TRUE(step.logIdle) << "starting to no input: one idle line";
	EXPECT_EQ(a.idleReason, (Int)AOC_CONVERTER_IDLE_NO_INPUT);

	AoCResourceBank iron;
	iron.init(nullptr);
	iron.deposit(IRON, 2);
	AoCConverterState b;
	step = AoCConverter::endCycle(b, r, facts, &iron, 120);
	EXPECT_TRUE(step.ran);
	EXPECT_FALSE(step.logIdle);
	EXPECT_EQ(b.idleReason, (Int)AOC_CONVERTER_IDLE_NONE);
	EXPECT_TRUE(b.running);
}

// Review F3, finding 2: any disable but the two power types (EMP, hacked, subdued, a script's disable) pauses the cycle
// with the running state cleared and the reason DISABLED, whatever RequiresPower says; it ranks after neutral and before
// no power; the first retry after it lifts converts again.
TEST_F(AoCConverterTest, DisabledPausesAndClearsRunning)
{
	defineStandardTypes();
	AoCSimRate::g_framesPerSecond = 30;
	const AoCConverterRecipe powered = mill(TRUE);
	const AoCConverterRecipe unpowered = mill(FALSE);
	EXPECT_STREQ(AoCConverter::reasonName(AOC_CONVERTER_IDLE_DISABLED), "disabled");

	AoCConverterFacts f;
	f.disabled = TRUE;
	EXPECT_EQ(AoCConverter::blockedReason(unpowered, f), AOC_CONVERTER_IDLE_DISABLED) << "RequiresPower = No still stops";
	f.underpowered = TRUE;
	EXPECT_EQ(AoCConverter::blockedReason(powered, f), AOC_CONVERTER_IDLE_DISABLED) << "disabled before no power";
	f.neutral = TRUE;
	EXPECT_EQ(AoCConverter::blockedReason(powered, f), AOC_CONVERTER_IDLE_NEUTRAL) << "neutral before disabled";

	AoCResourceBank bank;
	bank.init(nullptr);
	bank.deposit(IRON, 10);
	AoCConverterState state;
	AoCConverterFacts ok;
	EXPECT_TRUE(AoCConverter::endCycle(state, unpowered, ok, &bank, 120).ran);
	EXPECT_TRUE(state.running);

	AoCConverterFacts emp;
	emp.disabled = TRUE;
	AoCConverterStep step = AoCConverter::endCycle(state, unpowered, emp, &bank, 240);
	EXPECT_FALSE(step.ran);
	EXPECT_TRUE(step.logIdle);
	EXPECT_FALSE(state.running) << "the running condition clears (D8 3.5: smoke only while converting)";
	EXPECT_EQ(state.idleReason, (Int)AOC_CONVERTER_IDLE_DISABLED);
	EXPECT_EQ(bank.count(IRON), 8u) << "nothing withdrawn while disabled";
	EXPECT_EQ(step.sleepFrames, 30u) << "retry in one second";
	step = AoCConverter::endCycle(state, unpowered, emp, &bank, 270);
	EXPECT_FALSE(step.logIdle) << "still disabled: no second line";

	step = AoCConverter::endCycle(state, unpowered, ok, &bank, 300);
	EXPECT_TRUE(step.ran) << "the first retry after the disable lifts converts";
	EXPECT_EQ(bank.count(IRON), 6u);
}

// Review F3, finding 4: a converter that was running loses its power: one idle line, running cleared, the bank kept.
TEST_F(AoCConverterTest, RunningThenLosingPower)
{
	defineStandardTypes();
	AoCSimRate::g_framesPerSecond = 30;
	const AoCConverterRecipe r = mill(TRUE);
	AoCResourceBank bank;
	bank.init(nullptr);
	bank.deposit(IRON, 10);
	AoCConverterState state;
	AoCConverterFacts ok;
	EXPECT_TRUE(AoCConverter::endCycle(state, r, ok, &bank, 120).ran);
	EXPECT_TRUE(AoCConverter::endCycle(state, r, ok, &bank, 240).ran);

	AoCConverterFacts brownout;
	brownout.underpowered = TRUE;
	AoCConverterStep step = AoCConverter::endCycle(state, r, brownout, &bank, 360);
	EXPECT_FALSE(step.ran);
	EXPECT_TRUE(step.logIdle) << "running to no power: one idle line";
	EXPECT_FALSE(state.running);
	EXPECT_EQ(state.idleReason, (Int)AOC_CONVERTER_IDLE_NO_POWER);
	EXPECT_EQ(bank.count(IRON), 6u);
	EXPECT_EQ(bank.count(STEEL), 2u);
	step = AoCConverter::endCycle(state, r, brownout, &bank, 390);
	EXPECT_FALSE(step.logIdle);
}

// Review F3, finding 1: two converters of one player due in the same frame, with input for one of them. Whichever the
// scheduler runs first converts and the other idles, so the order decides the bank. The heap order of same-frame
// updates is deterministic in an uninterrupted game but is rebuilt by a load (stock GameLogic::loadPostProcess), so a
// loaded game can part from the saving run here; F14 has to keep that order across a save.
TEST_F(AoCConverterTest, TwoConvertersOnOneBankTheOrderDecides)
{
	defineStandardTypes();
	AoCSimRate::g_framesPerSecond = 30;
	const AoCConverterRecipe own = mill();	// 2 iron to 1 steel
	AoCConverterRecipe captured;	// a captured China mill, 3 iron to 2 steel (D11 2.3: the ratio is data)
	parse(AoCConverterRecipe::parseInputType, captured, "InputType = Iron");
	parse(AoCConverterRecipe::parseInputAmount, captured, "InputAmount = 3");
	parse(AoCConverterRecipe::parseOutputType, captured, "OutputType = Steel");
	parse(AoCConverterRecipe::parseOutputAmount, captured, "OutputAmount = 2");
	parse(AoCConverterRecipe::parseCycleTime, captured, "CycleTime = 4000");
	captured.finish();
	AoCConverterFacts facts;

	// Own mill first.
	AoCResourceBank first;
	first.init(nullptr);
	first.deposit(IRON, 3);
	AoCConverterState ownA, capturedA;
	EXPECT_TRUE(AoCConverter::endCycle(ownA, own, facts, &first, 120).ran);
	EXPECT_FALSE(AoCConverter::endCycle(capturedA, captured, facts, &first, 120).ran);
	EXPECT_EQ(capturedA.idleReason, (Int)AOC_CONVERTER_IDLE_NO_INPUT);
	EXPECT_EQ(first.count(IRON), 1u);
	EXPECT_EQ(first.count(STEEL), 1u);

	// Captured mill first.
	AoCResourceBank second;
	second.init(nullptr);
	second.deposit(IRON, 3);
	AoCConverterState ownB, capturedB;
	EXPECT_TRUE(AoCConverter::endCycle(capturedB, captured, facts, &second, 120).ran);
	EXPECT_FALSE(AoCConverter::endCycle(ownB, own, facts, &second, 120).ran);
	EXPECT_EQ(ownB.idleReason, (Int)AOC_CONVERTER_IDLE_NO_INPUT);
	EXPECT_EQ(second.count(IRON), 0u);
	EXPECT_EQ(second.count(STEEL), 2u);

	EXPECT_NE(first.count(STEEL), second.count(STEEL)) << "the same frame, the same bank, another order: another bank";
	EXPECT_NE(ownA.cycleEndFrame, ownB.cycleEndFrame) << "and the two converters' next cycle ends swap";
}

// Review F3, finding 5: the retry is converted once at parse time, like the cycle, so a cycle end only adds integers.
TEST_F(AoCConverterTest, RetryFramesAreSetAtParseTime)
{
	defineStandardTypes();
	AoCSimRate::g_framesPerSecond = 30;
	AoCConverterRecipe at30 = mill();
	EXPECT_EQ(at30.retryFrames, 30u);
	AoCSimRate::g_framesPerSecond = 60;
	AoCConverterRecipe at60 = mill();
	EXPECT_EQ(at60.retryFrames, 60u);
	// A cycle end uses the stored value, not the live rate: the 30 Hz recipe still retries in 30 frames.
	AoCResourceBank empty;
	empty.init(nullptr);
	AoCConverterState state;
	AoCConverterFacts facts;
	EXPECT_EQ(AoCConverter::endCycle(state, at30, facts, &empty, 120).sleepFrames, 30u);
	AoCConverterRecipe unfinished;
	EXPECT_EQ(unfinished.retryFrames, 0u) << "set only by finish";
}

TEST_F(AoCConverterTest, CostTextForTheLog)
{
	defineStandardTypes();
	char text[64];
	AoCCost c;
	AoCConverter::costText(c, text, sizeof(text));
	EXPECT_STREQ(text, "none");
	c.amount[IRON] = 2;
	AoCConverter::costText(c, text, sizeof(text));
	EXPECT_STREQ(text, "Iron:2");
	c.amount[FOOD] = 1;
	AoCConverter::costText(c, text, sizeof(text));
	EXPECT_STREQ(text, "Food:1 Iron:2");
}

// Decision 2 and review F3, finding 4: the three runtime fields round-trip through AoCConverterState::xfer, the call the
// module's xfer makes after its version byte and the UpdateModule block; the bytes are exactly the three fields in their
// order (so a reordering in the state shows here); a reason outside the enum is clamped on load; a block older than
// version 1 carries none; and the CRC covers each field.
TEST_F(AoCConverterTest, StateRoundTripsThroughItsXfer)
{
	const std::string path = ::testing::TempDir() + "AoCConverterStateRoundTrip.sav";
	{
		StateHolder holder;
		holder.state.cycleEndFrame = 1920;
		holder.state.running = FALSE;
		holder.state.idleReason = AOC_CONVERTER_IDLE_NO_POWER;
		XferSave save;
		save.open(path.c_str());
		save.xferSnapshot(&holder);
		save.close();
	}
	{
		const std::vector<unsigned char> bytes = readFile(path);
		ASSERT_EQ(bytes.size(), sizeof(UnsignedInt) + sizeof(Bool) + sizeof(Int)) << "the three fields and nothing else";
		UnsignedInt frame = 0;
		Int reason = 0;
		memcpy(&frame, &bytes[0], sizeof(frame));
		memcpy(&reason, &bytes[sizeof(UnsignedInt) + sizeof(Bool)], sizeof(reason));
		EXPECT_EQ(frame, 1920u) << "first the cycle end frame";
		EXPECT_EQ(bytes[sizeof(UnsignedInt)], 0u) << "then running";
		EXPECT_EQ(reason, (Int)AOC_CONVERTER_IDLE_NO_POWER) << "then the idle reason";

		StateHolder loaded;
		XferLoad load;
		load.setOptions(XO_NO_POST_PROCESSING);
		load.open(path.c_str());
		load.xferSnapshot(&loaded);
		load.close();
		EXPECT_EQ(loaded.state.cycleEndFrame, 1920u) << "the sleep resumes from the saved cycle end";
		EXPECT_FALSE(loaded.state.running);
		EXPECT_EQ(loaded.state.idleReason, (Int)AOC_CONVERTER_IDLE_NO_POWER);
	}
	{
		StateHolder holder;
		holder.state.cycleEndFrame = 240;
		holder.state.running = TRUE;
		holder.state.idleReason = AOC_CONVERTER_IDLE_NONE;
		XferSave save;
		save.open(path.c_str());
		save.xferSnapshot(&holder);
		save.close();
		StateHolder loaded;
		XferLoad load;
		load.setOptions(XO_NO_POST_PROCESSING);
		load.open(path.c_str());
		load.xferSnapshot(&loaded);
		load.close();
		EXPECT_TRUE(loaded.state.running);
		EXPECT_EQ(loaded.state.idleReason, (Int)AOC_CONVERTER_IDLE_NONE);
	}
	remove(path.c_str());
}

TEST_F(AoCConverterTest, LoadClampsAnUnknownReasonAndOlderBlocksCarryNone)
{
	const std::string path = ::testing::TempDir() + "AoCConverterStateClamp.sav";
	const Int bad[2] = { 99, -3 };
	for (Int n = 0; n < 2; ++n)
	{
		for (Int running = 0; running < 2; ++running)
		{
			StateHolder holder;
			holder.state.cycleEndFrame = 300;
			holder.state.running = running ? TRUE : FALSE;
			holder.state.idleReason = bad[n];
			XferSave save;
			save.open(path.c_str());
			save.xferSnapshot(&holder);
			save.close();
			StateHolder loaded;
			XferLoad load;
			load.setOptions(XO_NO_POST_PROCESSING);
			load.open(path.c_str());
			load.xferSnapshot(&loaded);
			load.close();
			EXPECT_EQ(loaded.state.cycleEndFrame, 300u);
			EXPECT_EQ(loaded.state.idleReason, running ? (Int)AOC_CONVERTER_IDLE_NONE : (Int)AOC_CONVERTER_IDLE_STARTING)
				<< "reason " << bad[n] << ", running " << running;
		}
	}

	// A block of version 0 (none exists; the rule for any field added later) reads nothing and keeps the constructor's state.
	{
		StateHolder holder;
		holder.version = 0;
		holder.state.cycleEndFrame = 777;
		XferSave save;
		save.open(path.c_str());
		save.xferSnapshot(&holder);
		save.close();
		EXPECT_EQ(readFile(path).size(), 0u);
		StateHolder loaded;
		loaded.version = 0;
		XferLoad load;
		load.setOptions(XO_NO_POST_PROCESSING);
		load.open(path.c_str());
		load.xferSnapshot(&loaded);
		load.close();
		EXPECT_EQ(loaded.state.cycleEndFrame, 0u);
		EXPECT_EQ(loaded.state.idleReason, (Int)AOC_CONVERTER_IDLE_STARTING);
	}
	remove(path.c_str());
}

TEST_F(AoCConverterTest, CrcCoversEachField)
{
	StateHolder a, b;
	const UnsignedInt base = crcOf(a);
	EXPECT_EQ(crcOf(b), base);
	b.state.cycleEndFrame = 120;
	EXPECT_NE(crcOf(b), base) << "the cycle end frame is in the CRC";
	b.state.cycleEndFrame = 0;
	b.state.running = TRUE;
	EXPECT_NE(crcOf(b), base) << "running is in the CRC";
	b.state.running = FALSE;
	b.state.idleReason = AOC_CONVERTER_IDLE_NO_INPUT;
	EXPECT_NE(crcOf(b), base) << "the idle reason is in the CRC";
}
