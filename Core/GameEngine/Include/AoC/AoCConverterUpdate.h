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

// AoCConverterUpdate.h
// Age of Commanders F3 (design D11 section 2.3): the converter module. It turns banked input into banked output on a
// timer (iron to steel at the mill) and, without an input, is a passive producer (the livestock farm's food, D2
// section 2.4), so no second module is needed:
//
//   Behavior = AoCConverterUpdate ModuleTag_Convert
//     InputType        = Iron      ; omit for a passive producer (InputAmount is then ignored)
//     InputAmount      = 2         ; whole number, at least 1 (default 1)
//     OutputType       = Steel     ; required
//     OutputAmount     = 1         ; whole number, at least 1 (default 1)
//     CycleTime        = 4000      ; ms, at least 1; frames at the live simulation rate (F15), as INI durations
//     RequiresPower    = Yes       ; default No; Yes pauses while the object is DISABLED_UNDERPOWERED
//     RunningCondition = USER_1    ; model condition set while a cycle runs; default none
//   End
//
// At each cycle end: if the object is under construction, sold, owned by the neutral player, disabled (EMP, hacked,
// subdued, a script's disable and the like), or (RequiresPower) underpowered, it records that idle reason, clears the
// condition and retries in one second; else, if the owner's bank holds the input, it withdraws the input, deposits the
// output, sets the condition and sleeps CycleTime; else it clears the condition, records "no input" and retries in one
// second. The first cycle ends CycleTime after the object appears; until then the reason is "starting". D8's "smoke
// only while converting" is the running condition, so it is off whenever the converter does not convert. Power draw is
// the stock EnergyProduction.
//
// Data rule: one object carries at most one converter per RunningCondition (two converters may share an object only
// with distinct conditions, or with none), because each converter clears its own condition when it idles. A debug
// build asserts on it when the object is created.
//
// Type names resolve through TheAoCRules at parse time, like AoCCost. With no AoCResourceType defined a template that
// carries the module is an INI error: the stock game never carries it, so stock stays byte-identical.
//
// The pure parts (the recipe, the runtime state, one cycle end) are free of the object so the unit tests can run them;
// the module applies them to its object.

#pragma once

#include "AoC/AoCCost.h"
#include "GameLogic/Module/UpdateModule.h"

class AoCResourceBank;
class Player;
class Xfer;

/// Why a converter is not running. Saved as an Int: append only.
enum AoCConverterIdleReason CPP_11(: Int)
{
	AOC_CONVERTER_IDLE_NONE = 0,						///< running (the last cycle end converted)
	AOC_CONVERTER_IDLE_NO_INPUT,						///< the owner's bank lacks the input
	AOC_CONVERTER_IDLE_NO_POWER,						///< RequiresPower and the object is underpowered
	AOC_CONVERTER_IDLE_UNDER_CONSTRUCTION,
	AOC_CONVERTER_IDLE_SOLD,
	AOC_CONVERTER_IDLE_NEUTRAL,							///< owned by the neutral player (or by none)
	AOC_CONVERTER_IDLE_DISABLED,						///< disabled by anything but the two power types (EMP, hacked, subdued, script)
	AOC_CONVERTER_IDLE_STARTING,						///< created, and its first cycle has not ended yet (F6 shows "starting")
	AOC_CONVERTER_IDLE_COUNT
};

/// What F6's steel tooltip reads per converter. Reached from an object the stock way: every BehaviorModule answers
/// getAoCConverterInterface(), NULL unless it is a converter. isRunning() is TRUE exactly while the running condition is
/// set; otherwise getIdleReason() says why (STARTING before the first cycle end).
class AoCConverterInterface
{
public:
	virtual Bool isRunning() const = 0;
	virtual AoCConverterIdleReason getIdleReason() const = 0;
};

/// The module data's own fields (D11 F3 decision 1): integers, the two vectors built once the block is read.
struct AoCConverterRecipe
{
	Int inputType;						///< InputType, -1 for a passive producer
	UnsignedInt inputAmount;	///< InputAmount, at least 1
	Int outputType;						///< OutputType, -1 until parsed (required)
	UnsignedInt outputAmount;	///< OutputAmount, at least 1
	UnsignedInt cycleMsecs;		///< CycleTime as written
	UnsignedInt cycleFrames;	///< CycleTime in frames at the live rate, converted at parse time
	UnsignedInt retryFrames;	///< the one-second retry in frames at the live rate, set by finish (no float at a cycle end)
	Bool requiresPower;				///< RequiresPower
	Int runningCondition;			///< RunningCondition, a ModelConditionFlagType; -1 none
	AoCCost input;						///< inputType:inputAmount, zero for a passive producer (built by finish)
	AoCCost output;						///< outputType:outputAmount (built by finish)

	AoCConverterRecipe();

	Bool isPassive() const { return inputType < 0; }

	/// After the block: OutputType and CycleTime are required (INI_INVALID_DATA otherwise); builds the input and output
	/// vectors so the bank's withdraw and deposit take them as they are, and sets retryFrames.
	void finish();

	/// The module needs resource types: INI_INVALID_DATA while TheAoCRules is disabled (stock data).
	static void requireResourceTypes();

	// FieldParse procedures; 'store' points at the AoCConverterRecipe.
	static void parseInputType( INI *ini, void *instance, void *store, const void *userData );
	static void parseInputAmount( INI *ini, void *instance, void *store, const void *userData );
	static void parseOutputType( INI *ini, void *instance, void *store, const void *userData );
	static void parseOutputAmount( INI *ini, void *instance, void *store, const void *userData );
	static void parseCycleTime( INI *ini, void *instance, void *store, const void *userData );
	static void parseRequiresPower( INI *ini, void *instance, void *store, const void *userData );
	static void parseRunningCondition( INI *ini, void *instance, void *store, const void *userData );
};

/// The runtime fields (D11 F3 decision 2), saved by the module's xfer and written into its crc.
struct AoCConverterState
{
	enum { XFER_VERSION = 1 };	///< the module's block version: 1, the three fields below

	UnsignedInt cycleEndFrame;	///< the frame of the next cycle end
	Bool running;								///< the last cycle end converted (the running condition is set)
	Int idleReason;							///< AoCConverterIdleReason; NONE while running, STARTING until the first cycle end

	AoCConverterState();

	/// The fields of the module's block, after its version byte and the UpdateModule block: AoCConverterUpdate::xfer
	/// calls this with the version it read. A loaded reason outside the enum becomes NONE when running, else STARTING.
	void xfer( Xfer *xfer, XferVersion version );
	void crc( Xfer *xfer );
};

/// What the object is at a cycle end, read by the module from its object.
struct AoCConverterFacts
{
	Bool underConstruction;		///< OBJECT_STATUS_UNDER_CONSTRUCTION
	Bool sold;								///< OBJECT_STATUS_SOLD
	Bool neutral;							///< owned by the neutral player, or by none
	Bool disabled;						///< any disable but the two power types (EMP, hacked, subdued, a script's disable ...)
	Bool underpowered;				///< DISABLED_UNDERPOWERED (or the script's DISABLED_SCRIPT_UNDERPOWERED)

	AoCConverterFacts() : underConstruction( FALSE ), sold( FALSE ), neutral( FALSE ), disabled( FALSE ), underpowered( FALSE ) {}
};

/// What one cycle end did, for the module to apply and log.
struct AoCConverterStep
{
	Bool ran;									///< the input was withdrawn and the output deposited
	Bool logIdle;							///< idle for a reason other than the previous cycle end's (the idle line, once per change)
	UnsignedInt sleepFrames;	///< CycleTime when it ran, one second otherwise
};

namespace AoCConverter
{
	/// The idle reason the object itself imposes (construction, sale, neutral owner, a disable, power), NONE when it
	/// may convert.
	AoCConverterIdleReason blockedReason( const AoCConverterRecipe &recipe, const AoCConverterFacts &facts );

	/// One cycle end at frame 'now': updates the state and, when it runs, withdraws the input from and deposits the output
	/// into 'bank' (the owner's; nullptr counts as a neutral owner). The next cycle end is now + step.sleepFrames.
	AoCConverterStep endCycle( AoCConverterState &state, const AoCConverterRecipe &recipe, const AoCConverterFacts &facts,
		AoCResourceBank *bank, UnsignedInt now );

	/// The one-second retry in frames, through the F15 helper (AoCGather::msecsToFrames), never a fixed count. Called at
	/// parse time (AoCConverterRecipe::finish), which stores it in the recipe.
	UnsignedInt retryFrames();

	/// "no-input", "no-power", "under-construction", "sold", "neutral", "disabled", "starting", "none".
	const char *reasonName( Int reason );

	/// "Iron:2" (every non-zero component, "Name:amount" separated by spaces), "none" for a zero vector.
	void costText( const AoCCost &cost, char *out, Int outSize );

	/// Evidence trail (ticket F3 decision 6): while any harness flag is given, "[AoC convert] frame N <objectId> <event>".
	/// Nothing is printed in a stock run; logic never reads it.
	Bool logEnabled();
	void log( UnsignedInt objectId, const char *format, ... );
}

//-------------------------------------------------------------------------------------------------
class AoCConverterUpdateModuleData : public UpdateModuleData
{
public:
	AoCConverterRecipe m_recipe;

	AoCConverterUpdateModuleData() {}

	static void buildFieldParse( MultiIniFieldParse &p );
};

//-------------------------------------------------------------------------------------------------
class AoCConverterUpdate : public UpdateModule, public AoCConverterInterface
{

	MEMORY_POOL_GLUE_WITH_EXPLICIT_CREATE( AoCConverterUpdate, "AoCConverterUpdate", 32, 32 )
	MAKE_STANDARD_MODULE_MACRO( AoCConverterUpdate )

private:
	const AoCConverterUpdateModuleData *getAoCConverterUpdateModuleData() const { return (const AoCConverterUpdateModuleData *)getModuleData(); }

public:

	/// Replaces MAKE_STANDARD_MODULE_DATA_MACRO_ABC's: refuses the module while no resource types are defined, and
	/// checks the block once it is read (AoCConverterRecipe::finish).
	static ModuleData *friend_newModuleData( INI *ini );

	AoCConverterUpdate( Thing *thing, const ModuleData *moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual AoCConverterInterface *getAoCConverterInterface() override { return this; }

	/// Every disable type: the module decides (RequiresPower = No keeps converting while underpowered, Yes records
	/// "no power"; any other disable records "disabled" and clears the condition), the same under either mask rule of
	/// GameLogic::update.
	virtual DisabledMaskType getDisabledTypesToProcess() const override;
	/// Debug builds: asserts the data rule (no second converter with the same RunningCondition on the object).
	virtual void onObjectCreated() override;
	virtual UpdateSleepTime update() override;

	// AoCConverterInterface
	virtual Bool isRunning() const override { return m_state.running; }
	virtual AoCConverterIdleReason getIdleReason() const override { return (AoCConverterIdleReason)m_state.idleReason; }

	/// Every converter module of the player's live objects, running or not (the -aiLog columns; F6's steel tooltip).
	static void countForPlayer( Player *player, Int &running, Int &idle );

protected:

	void applyRunningCondition( Bool running );
	void logCycleEnd( const AoCConverterStep &step, const Player *owner );
	void showDeposit( const Player *owner );

	AoCConverterState m_state;

};
