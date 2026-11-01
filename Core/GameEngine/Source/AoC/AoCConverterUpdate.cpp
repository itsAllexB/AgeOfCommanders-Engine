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

// AoCConverterUpdate.cpp
// Age of Commanders F3: the converter module (design D11 section 2.3). See AoCConverterUpdate.h.
// Patterns: AutoDepositUpdate (neutral owners skipped, the floating text under the stock local-player guard) and
// OCLUpdate (a countdown to a frame).

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCConverterUpdate.h"
#include "AoC/AoCGather.h"
#include "AoC/AoCHarness.h"
#include "AoC/AoCResourceBank.h"
#include "AoC/AoCRules.h"
#include "Common/INI.h"
#include "Common/ModelState.h"
#include "Common/Player.h"
#include "Common/Xfer.h"
#include "GameClient/ClientRandomValue.h"
#include "GameClient/Color.h"
#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------
// The recipe
// ------------------------------------------------------------------------------------------------
AoCConverterRecipe::AoCConverterRecipe()
{
	inputType = -1;
	inputAmount = 1;
	outputType = -1;
	outputAmount = 1;
	cycleMsecs = 0;
	cycleFrames = 0;
	requiresPower = FALSE;
	runningCondition = -1;
}

// ------------------------------------------------------------------------------------------------
void AoCConverterRecipe::finish()
{
	if (outputType < 0)
	{
		DEBUG_CRASH(("AoCConverterUpdate: OutputType is required"));
		throw INI_INVALID_DATA;
	}
	if (cycleFrames == 0)
	{
		DEBUG_CRASH(("AoCConverterUpdate: CycleTime is required"));
		throw INI_INVALID_DATA;
	}
	input.clear();
	if (inputType >= 0)
		input.amount[inputType] = inputAmount;
	output.clear();
	output.amount[outputType] = outputAmount;
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCConverterRecipe::requireResourceTypes()
{
	if (!AoCRules::enabled())
	{
		DEBUG_CRASH(("AoCConverterUpdate needs AoCResourceType definitions (Data\\INI\\AoCResources.ini)"));
		throw INI_INVALID_DATA;
	}
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCConverterRecipe::parseInputType( INI * /*ini*/, void * /*instance*/, void *store, const void * /*userData*/ )
{
	((AoCConverterRecipe *)store)->inputType = AoCGather::parseTypeName(INI::getNextToken());
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCConverterRecipe::parseInputAmount( INI *ini, void * /*instance*/, void *store, const void * /*userData*/ )
{
	AoCGather::parsePositiveUnsignedInt(ini, nullptr, &((AoCConverterRecipe *)store)->inputAmount, nullptr);
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCConverterRecipe::parseOutputType( INI * /*ini*/, void * /*instance*/, void *store, const void * /*userData*/ )
{
	((AoCConverterRecipe *)store)->outputType = AoCGather::parseTypeName(INI::getNextToken());
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCConverterRecipe::parseOutputAmount( INI *ini, void * /*instance*/, void *store, const void * /*userData*/ )
{
	AoCGather::parsePositiveUnsignedInt(ini, nullptr, &((AoCConverterRecipe *)store)->outputAmount, nullptr);
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCConverterRecipe::parseCycleTime( INI *ini, void * /*instance*/, void *store, const void * /*userData*/ )
{
	// Milliseconds as written, frames by the INI duration rule at the live rate (fixed before any INI loads, F15), as
	// F2's AoCGatherDelay: 4000 ms is 120 frames at 30 Hz and 240 at 60 Hz.
	AoCConverterRecipe *recipe = (AoCConverterRecipe *)store;
	AoCGather::parsePositiveUnsignedInt(ini, nullptr, &recipe->cycleMsecs, nullptr);
	recipe->cycleFrames = AoCGather::msecsToFrames(recipe->cycleMsecs);
	if (recipe->cycleFrames == 0)
		recipe->cycleFrames = 1;
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCConverterRecipe::parseRequiresPower( INI * /*ini*/, void * /*instance*/, void *store, const void * /*userData*/ )
{
	((AoCConverterRecipe *)store)->requiresPower = INI::scanBool(INI::getNextToken());
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCConverterRecipe::parseRunningCondition( INI * /*ini*/, void * /*instance*/, void *store, const void * /*userData*/ )
{
	AoCConverterRecipe *recipe = (AoCConverterRecipe *)store;
	const char *token = INI::getNextToken();
	if (stricmp(token, "None") == 0)
	{
		recipe->runningCondition = -1;
		return;
	}
	recipe->runningCondition = INI::scanIndexList(token, ModelConditionFlags::getBitNames());	// throws on an unknown name
}

// ------------------------------------------------------------------------------------------------
// The runtime state
// ------------------------------------------------------------------------------------------------
AoCConverterState::AoCConverterState()
{
	cycleEndFrame = 0;
	running = FALSE;
	idleReason = AOC_CONVERTER_IDLE_NONE;
}

// ------------------------------------------------------------------------------------------------
void AoCConverterState::xfer( Xfer *xfer )
{
	xfer->xferUnsignedInt( &cycleEndFrame );
	xfer->xferBool( &running );
	xfer->xferInt( &idleReason );
	if (xfer->getXferMode() == XFER_LOAD && (idleReason < AOC_CONVERTER_IDLE_NONE || idleReason >= AOC_CONVERTER_IDLE_COUNT))
		idleReason = AOC_CONVERTER_IDLE_NONE;
}

// ------------------------------------------------------------------------------------------------
void AoCConverterState::crc( Xfer *xfer )
{
	xfer->xferUnsignedInt( &cycleEndFrame );
	xfer->xferBool( &running );
	xfer->xferInt( &idleReason );
}

// ------------------------------------------------------------------------------------------------
// One cycle end
// ------------------------------------------------------------------------------------------------
AoCConverterIdleReason AoCConverter::blockedReason( const AoCConverterRecipe &recipe, const AoCConverterFacts &facts )
{
	if (facts.underConstruction)
		return AOC_CONVERTER_IDLE_UNDER_CONSTRUCTION;
	if (facts.sold)
		return AOC_CONVERTER_IDLE_SOLD;
	if (facts.neutral)
		return AOC_CONVERTER_IDLE_NEUTRAL;
	if (recipe.requiresPower && facts.underpowered)
		return AOC_CONVERTER_IDLE_NO_POWER;
	return AOC_CONVERTER_IDLE_NONE;
}

// ------------------------------------------------------------------------------------------------
AoCConverterStep AoCConverter::endCycle( AoCConverterState &state, const AoCConverterRecipe &recipe, const AoCConverterFacts &facts,
	AoCResourceBank *bank, UnsignedInt now )
{
	AoCConverterStep step;
	step.ran = FALSE;
	step.logIdle = FALSE;
	step.sleepFrames = retryFrames();

	AoCConverterIdleReason reason = blockedReason(recipe, facts);
	if (reason == AOC_CONVERTER_IDLE_NONE && bank == nullptr)
		reason = AOC_CONVERTER_IDLE_NEUTRAL;
	if (reason == AOC_CONVERTER_IDLE_NONE)
	{
		// A passive producer's input is the zero vector, which every bank affords and nothing withdraws.
		if (bank->canAfford(recipe.input) && (recipe.isPassive() || bank->withdraw(recipe.input, FALSE)))
		{
			bank->deposit(recipe.output);
			step.ran = TRUE;
			step.sleepFrames = recipe.cycleFrames;
		}
		else
		{
			reason = AOC_CONVERTER_IDLE_NO_INPUT;
		}
	}

	if (step.ran)
	{
		state.running = TRUE;
		state.idleReason = AOC_CONVERTER_IDLE_NONE;
	}
	else
	{
		step.logIdle = state.running || state.idleReason != (Int)reason;
		state.running = FALSE;
		state.idleReason = reason;
	}

	if (step.sleepFrames < 1)
		step.sleepFrames = 1;
	state.cycleEndFrame = now + step.sleepFrames;
	return step;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCConverter::retryFrames()
{
	const UnsignedInt frames = AoCGather::msecsToFrames(1000);
	return frames > 0 ? frames : 1;
}

// ------------------------------------------------------------------------------------------------
const char *AoCConverter::reasonName( Int reason )
{
	switch (reason)
	{
		case AOC_CONVERTER_IDLE_NONE: return "none";
		case AOC_CONVERTER_IDLE_NO_INPUT: return "no-input";
		case AOC_CONVERTER_IDLE_NO_POWER: return "no-power";
		case AOC_CONVERTER_IDLE_UNDER_CONSTRUCTION: return "under-construction";
		case AOC_CONVERTER_IDLE_SOLD: return "sold";
		case AOC_CONVERTER_IDLE_NEUTRAL: return "neutral";
		default: return "?";
	}
}

// ------------------------------------------------------------------------------------------------
void AoCConverter::costText( const AoCCost &cost, char *out, Int outSize )
{
	if (out == nullptr || outSize <= 0)
		return;
	out[0] = '\0';
	Int used = 0;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
	{
		if (cost.amount[i] == 0)
			continue;
		const int n = snprintf(out + used, (size_t)(outSize - used), "%s%s:%u", used > 0 ? " " : "", AoCGather::typeName(i), cost.amount[i]);
		if (n < 0 || used + n >= outSize)
			break;
		used += n;
	}
	if (used == 0)
		snprintf(out, (size_t)outSize, "none");
}

// ------------------------------------------------------------------------------------------------
Bool AoCConverter::logEnabled()
{
	return AoCHarness::isActive();
}

// ------------------------------------------------------------------------------------------------
void AoCConverter::log( UnsignedInt objectId, const char *format, ... )
{
	if (!logEnabled())
		return;
	char text[512];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	text[sizeof(text) - 1] = '\0';
	fprintf(stderr, "[AoC convert] frame %u %u %s\n", TheGameLogic ? TheGameLogic->getFrame() : 0u, objectId, text);
	fflush(stderr);
}

// ------------------------------------------------------------------------------------------------
// The module
// ------------------------------------------------------------------------------------------------
/*static*/ void AoCConverterUpdateModuleData::buildFieldParse( MultiIniFieldParse &p )
{
	UpdateModuleData::buildFieldParse(p);
	static const FieldParse dataFieldParse[] =
	{
		{ "InputType",				AoCConverterRecipe::parseInputType,					nullptr, offsetof( AoCConverterUpdateModuleData, m_recipe ) },
		{ "InputAmount",			AoCConverterRecipe::parseInputAmount,				nullptr, offsetof( AoCConverterUpdateModuleData, m_recipe ) },
		{ "OutputType",				AoCConverterRecipe::parseOutputType,				nullptr, offsetof( AoCConverterUpdateModuleData, m_recipe ) },
		{ "OutputAmount",			AoCConverterRecipe::parseOutputAmount,			nullptr, offsetof( AoCConverterUpdateModuleData, m_recipe ) },
		{ "CycleTime",				AoCConverterRecipe::parseCycleTime,					nullptr, offsetof( AoCConverterUpdateModuleData, m_recipe ) },
		{ "RequiresPower",		AoCConverterRecipe::parseRequiresPower,			nullptr, offsetof( AoCConverterUpdateModuleData, m_recipe ) },
		{ "RunningCondition",	AoCConverterRecipe::parseRunningCondition,	nullptr, offsetof( AoCConverterUpdateModuleData, m_recipe ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

// ------------------------------------------------------------------------------------------------
/*static*/ ModuleData *AoCConverterUpdate::friend_newModuleData( INI *ini )
{
	// Refused before the fields are read, so the INI error names the Behavior line; a missing OutputType or CycleTime
	// is reported at the block's End.
	if (ini)
		AoCConverterRecipe::requireResourceTypes();
	AoCConverterUpdateModuleData *data = MSGNEW( "AllModuleData" ) AoCConverterUpdateModuleData;
	if (ini)
	{
		try
		{
			ini->initFromINIMultiProc(data, AoCConverterUpdateModuleData::buildFieldParse);
			data->m_recipe.finish();
		}
		catch (...)
		{
			delete data;	// not yet handed to the module factory
			throw;
		}
	}
	return data;
}

// ------------------------------------------------------------------------------------------------
AoCConverterUpdate::AoCConverterUpdate( Thing *thing, const ModuleData *moduleData ) : UpdateModule( thing, moduleData )
{
	// The first cycle ends one CycleTime after the object appears (a save load overwrites this in xfer).
	m_state.cycleEndFrame = TheGameLogic->getFrame() + getAoCConverterUpdateModuleData()->m_recipe.cycleFrames;
}

// ------------------------------------------------------------------------------------------------
AoCConverterUpdate::~AoCConverterUpdate()
{
}

// ------------------------------------------------------------------------------------------------
DisabledMaskType AoCConverterUpdate::getDisabledTypesToProcess() const
{
	return MAKE_DISABLED_MASK2( DISABLED_UNDERPOWERED, DISABLED_SCRIPT_UNDERPOWERED );
}

// ------------------------------------------------------------------------------------------------
UpdateSleepTime AoCConverterUpdate::update()
{
	const UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_state.cycleEndFrame)
		return UPDATE_SLEEP( m_state.cycleEndFrame - now );	// the first call, or a wake before the cycle end

	Object *obj = getObject();
	Player *owner = obj->getControllingPlayer();

	AoCConverterFacts facts;
	facts.underConstruction = obj->testStatus( OBJECT_STATUS_UNDER_CONSTRUCTION );
	facts.sold = obj->testStatus( OBJECT_STATUS_SOLD );
	facts.neutral = (owner == nullptr) || obj->isNeutralControlled();
	facts.underpowered = obj->isDisabledByType( DISABLED_UNDERPOWERED ) || obj->isDisabledByType( DISABLED_SCRIPT_UNDERPOWERED );

	const AoCConverterStep step = AoCConverter::endCycle( m_state, getAoCConverterUpdateModuleData()->m_recipe, facts,
		owner ? owner->getAoCBank() : nullptr, now );

	applyRunningCondition( step.ran );
	logCycleEnd( step, owner );
	if (step.ran)
		showDeposit( owner );

	return UPDATE_SLEEP( step.sleepFrames );
}

// ------------------------------------------------------------------------------------------------
void AoCConverterUpdate::applyRunningCondition( Bool running )
{
	const Int condition = getAoCConverterUpdateModuleData()->m_recipe.runningCondition;
	if (condition < 0)
		return;
	if (running)
		getObject()->setModelConditionState( (ModelConditionFlagType)condition );
	else
		getObject()->clearModelConditionState( (ModelConditionFlagType)condition );
}

// ------------------------------------------------------------------------------------------------
void AoCConverterUpdate::logCycleEnd( const AoCConverterStep &step, const Player *owner )
{
	if (!AoCConverter::logEnabled() || (!step.ran && !step.logIdle))
		return;
	const AoCConverterRecipe &recipe = getAoCConverterUpdateModuleData()->m_recipe;

	// The owner's amounts of the recipe's types after the cycle end.
	char bank[128];
	bank[0] = '\0';
	if (owner)
	{
		Int used = 0;
		const Int types[2] = { recipe.inputType, recipe.outputType };
		for (Int n = 0; n < 2; ++n)
		{
			if (types[n] < 0)
				continue;
			const int w = snprintf(bank + used, sizeof(bank) - used, "%s%s=%u", used > 0 ? " " : "", AoCGather::typeName(types[n]),
				owner->getAoCBank()->count(types[n]));
			if (w < 0 || used + w >= (Int)sizeof(bank))
				break;
			used += w;
		}
	}

	// The running condition as the drawable holds it, read for this line only (evidence that the flag reached the model).
	char condition[64];
	condition[0] = '\0';
	if (recipe.runningCondition >= 0)
	{
		const Drawable *draw = getObject()->getDrawable();
		snprintf(condition, sizeof(condition), " %s %s", ModelConditionFlags::getNameFromSingleBit(recipe.runningCondition),
			draw ? (draw->getModelConditionFlags().test(recipe.runningCondition) ? "on" : "off") : "no-drawable");
	}

	const Int playerIndex = owner ? (Int)owner->getPlayerIndex() : -1;
	if (step.ran)
	{
		char in[96];
		char out[96];
		AoCConverter::costText(recipe.input, in, sizeof(in));
		AoCConverter::costText(recipe.output, out, sizeof(out));
		AoCConverter::log((UnsignedInt)getObject()->getID(), "ran %s -> %s bank %s player %d%s", in, out, bank, (int)playerIndex, condition);
	}
	else
	{
		AoCConverter::log((UnsignedInt)getObject()->getID(), "idle %s bank %s player %d%s", AoCConverter::reasonName(m_state.idleReason),
			bank, (int)playerIndex, condition);
	}
}

// ------------------------------------------------------------------------------------------------
void AoCConverterUpdate::showDeposit( const Player *owner )
{
	const Object *obj = getObject();
	if (owner == nullptr || !obj->isLogicallyVisible())
		return;

	// OY LOOK!  I AM USING LOCAL PLAYER.  Do not put anything other than TheInGameUI->addFloatingText in the block this controls!!!
	const AoCConverterRecipe &recipe = getAoCConverterUpdateModuleData()->m_recipe;
	UnicodeString text = AoCGather::depositText( recipe.outputType, recipe.outputAmount );
	Coord3D pos;
	pos.set( *obj->getPosition() );
	pos.z += 10.0f;	// a little z to show it above the object, as AutoDepositUpdate does
	if (obj->isKindOf( KINDOF_STRUCTURE ))
	{
		const Real width = obj->getGeometryInfo().getMajorRadius() * 0.3f;
		const Real depth = obj->getGeometryInfo().getMinorRadius() * 0.3f;
		pos.x += GameClientRandomValueReal( -width, width );
		pos.y += GameClientRandomValueReal( -depth, depth );
	}
	const Color color = owner->getPlayerColor() | GameMakeColor( 0, 0, 0, 230 );
	TheInGameUI->addFloatingText( text, &pos, color );
}

// ------------------------------------------------------------------------------------------------
namespace
{
struct ConverterCount
{
	Int running;
	Int idle;
};

void countConverters( Object *obj, void *userData )
{
	ConverterCount *c = (ConverterCount *)userData;
	if (obj == nullptr || obj->isEffectivelyDead())
		return;
	for (BehaviorModule **m = obj->getBehaviorModules(); *m; ++m)
	{
		const AoCConverterInterface *converter = (*m)->getAoCConverterInterface();
		if (converter == nullptr)
			continue;
		if (converter->isRunning())
			c->running++;
		else
			c->idle++;
	}
}
}

/*static*/ void AoCConverterUpdate::countForPlayer( Player *player, Int &running, Int &idle )
{
	ConverterCount c;
	c.running = 0;
	c.idle = 0;
	if (player)
		player->iterateObjects(countConverters, &c);
	running = c.running;
	idle = c.idle;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AoCConverterUpdate::crc( Xfer *xfer )
{
	// extend base class
	UpdateModule::crc( xfer );

	m_state.crc( xfer );
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version: the cycle end frame, running, the idle reason (D11 F3) */
// ------------------------------------------------------------------------------------------------
void AoCConverterUpdate::xfer( Xfer *xfer )
{
	// version
	const XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class (it saves the next call frame, so the sleep resumes on load)
	UpdateModule::xfer( xfer );

	m_state.xfer( xfer );
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AoCConverterUpdate::loadPostProcess()
{
	// extend base class
	UpdateModule::loadPostProcess();
}
