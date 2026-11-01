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

// AoCTimedDropUpdate.cpp
// Age of Commanders F11: the timed drop module behind the Airlift Contract. See AoCTimedDropUpdate.h. The timer is
// OCLUpdate::update's (GeneralsMD OCLUpdate.cpp, the TheSuperHackers branch), copied.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCTimedDropUpdate.h"

#include "AoC/AoCGather.h"
#include "AoC/AoCHarness.h"
#include "AoC/AoCTrade.h"
#include "Common/INI.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/LogicRandomValue.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/TerrainLogic.h"

#include <stdarg.h>
#include <stdio.h>

// ------------------------------------------------------------------------------------------------
// The pure timer
// ------------------------------------------------------------------------------------------------
AoCTimedDropState::AoCTimedDropState()
{
	nextDropFrame = 0;
	timerStartFrame = 0;
	armed = FALSE;
}

// ------------------------------------------------------------------------------------------------
void AoCTimedDropState::xfer( Xfer *xfer, XferVersion version )
{
	if (version < 1)
		return;
	xfer->xferUnsignedInt( &nextDropFrame );
	xfer->xferUnsignedInt( &timerStartFrame );
	xfer->xferBool( &armed );
}

// ------------------------------------------------------------------------------------------------
void AoCTimedDropState::crc( Xfer *xfer )
{
	xfer->xferUnsignedInt( &nextDropFrame );
	xfer->xferUnsignedInt( &timerStartFrame );
	xfer->xferBool( &armed );
}

// ------------------------------------------------------------------------------------------------
Bool AoCTimedDrop::arm( AoCTimedDropState &state, Bool hasUpgrade )
{
	if (state.armed || !hasUpgrade)
		return FALSE;
	state.armed = TRUE;
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
Bool AoCTimedDrop::needsDelay( const AoCTimedDropState &state, UnsignedInt now, Bool underConstruction, Bool disabled )
{
	// The timer runs once its first start has set a next-drop frame (OCLUpdate tests m_timerStartedFrame > 0, which a
	// start at frame 0 leaves at 0; the next-drop frame is the same test without that edge).
	const Bool running = state.nextDropFrame != 0;
	if (running && disabled)
		return FALSE;
	return now >= state.nextDropFrame && !underConstruction;
}

// ------------------------------------------------------------------------------------------------
AoCTimedDropStep AoCTimedDrop::armedUpdate( AoCTimedDropState &state, UnsignedInt now, Bool underConstruction, Bool disabled, UnsignedInt delay )
{
	const Bool running = state.nextDropFrame != 0;
	// TheSuperHackers @bugfix in OCLUpdate (dizzyj/Caball009/Mauller 14/07/2025): a subdued building's timer shifts
	// instead of running out, but only once it has started, so the timer is set up when construction ends.
	if (running && disabled)
	{
		++state.nextDropFrame;
		++state.timerStartFrame;
		return AOC_TIMED_DROP_SHIFTED;
	}
	if (now < state.nextDropFrame || underConstruction)
		return AOC_TIMED_DROP_WAIT;
	// "You don't get to actually spread the first try, you start on a timer, then go" (OCLUpdate)
	state.timerStartFrame = now;
	state.nextDropFrame = now + delay;
	return running ? AOC_TIMED_DROP_DROP : AOC_TIMED_DROP_STARTED;
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCTimedDrop::remainingFrames( const AoCTimedDropState &state, UnsignedInt now )
{
	if (state.nextDropFrame == 0 || now >= state.nextDropFrame)
		return 0;
	return state.nextDropFrame - now;
}

// ------------------------------------------------------------------------------------------------
Real AoCTimedDrop::countdownPercent( const AoCTimedDropState &state, UnsignedInt now )
{
	if (state.nextDropFrame == 0)
		return 0.0f;
	const UnsignedInt total = state.nextDropFrame - state.timerStartFrame;
	if (total == 0 || now >= state.nextDropFrame)
		return 1.0f;
	return 1.0f - ((Real)(state.nextDropFrame - now) / (Real)total);	// display only
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCTimedDrop::pollFrames()
{
	const UnsignedInt frames = AoCGather::msecsToFrames(1000);
	return frames > 0 ? frames : 1;
}

// ------------------------------------------------------------------------------------------------
UnicodeString AoCTimedDrop::chipText( UnsignedInt remainingFrames )
{
	const UnsignedInt fps = (UnsignedInt)LOGICFRAMES_PER_SECOND;
	const UnsignedInt seconds = fps > 0 ? (remainingFrames + fps - 1) / fps : 0;	// a started second shows as a whole one
	UnicodeString text;
	if (TheGameText)
		text.format(TheGameText->FETCH_OR_SUBSTITUTE("GUI:AoCNextAirdrop", L"Next airdrop %d:%02d"), (Int)(seconds / 60), (Int)(seconds % 60));
	else
		text.format(L"Next airdrop %d:%02d", (Int)(seconds / 60), (Int)(seconds % 60));
	return text;
}

// ------------------------------------------------------------------------------------------------
namespace
{
/// Evidence trail (ticket F11 decision 5): while any harness flag is given, "[AoC drop] frame F player N <event>".
/// Nothing is printed in a run without flags; logic never reads it.
void dropLog( const Object *obj, const char *format, ... )
{
	if (!AoCHarness::isActive())
		return;
	char text[512];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	text[sizeof(text) - 1] = '\0';
	const Player *owner = obj ? obj->getControllingPlayer() : nullptr;
	fprintf(stderr, "[AoC drop] frame %u player %d %s\n", TheGameLogic ? TheGameLogic->getFrame() : 0u,
		owner ? (int)owner->getPlayerIndex() : -1, text);
	fflush(stderr);
}
}

// ------------------------------------------------------------------------------------------------
// Module data
// ------------------------------------------------------------------------------------------------
AoCTimedDropUpdateModuleData::AoCTimedDropUpdateModuleData()
{
	m_ocl = nullptr;
	m_minDelay = 0;
	m_maxDelay = 0;
	m_createAtEdge = FALSE;
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCTimedDropUpdateModuleData::parseOCL( INI * /*ini*/, void *instance, void * /*store*/, const void * /*userData*/ )
{
	// The name is kept for the [AoC drop] line; the list itself must exist (the creation lists load before the objects).
	AoCTimedDropUpdateModuleData *self = (AoCTimedDropUpdateModuleData *)instance;
	const char *token = INI::getNextToken();
	self->m_oclName = token;
	self->m_ocl = TheObjectCreationListStore ? TheObjectCreationListStore->findObjectCreationList(token) : nullptr;
	if (self->m_ocl == nullptr && TheObjectCreationListStore)
	{
		DEBUG_CRASH(("AoCTimedDropUpdate: OCL '%s' is not an object creation list", token));
		throw INI_INVALID_DATA;
	}
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCTimedDropUpdateModuleData::buildFieldParse( MultiIniFieldParse &p )
{
	UpdateModuleData::buildFieldParse(p);
	static const FieldParse dataFieldParse[] =
	{
		{ "OCL",							AoCTimedDropUpdateModuleData::parseOCL,	nullptr, 0 },
		{ "MinDelay",					INI::parseDurationUnsignedInt,					nullptr, offsetof( AoCTimedDropUpdateModuleData, m_minDelay ) },
		{ "MaxDelay",					INI::parseDurationUnsignedInt,					nullptr, offsetof( AoCTimedDropUpdateModuleData, m_maxDelay ) },
		{ "CreateAtEdge",			INI::parseBool,													nullptr, offsetof( AoCTimedDropUpdateModuleData, m_createAtEdge ) },
		{ "RequiredUpgrade",	INI::parseAsciiString,									nullptr, offsetof( AoCTimedDropUpdateModuleData, m_requiredUpgrade ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

// ------------------------------------------------------------------------------------------------
// The module
// ------------------------------------------------------------------------------------------------
AoCTimedDropUpdate::AoCTimedDropUpdate( Thing *thing, const ModuleData *moduleData ) : UpdateModule( thing, moduleData )
{
	m_upgrade = nullptr;
	m_upgradeResolved = FALSE;
}

// ------------------------------------------------------------------------------------------------
AoCTimedDropUpdate::~AoCTimedDropUpdate()
{
}

// ------------------------------------------------------------------------------------------------
Bool AoCTimedDropUpdate::hasRequiredUpgrade() const
{
	const AoCTimedDropUpdateModuleData *data = getAoCTimedDropUpdateModuleData();
	if (data->m_requiredUpgrade.isEmpty())
		return TRUE;
	if (!m_upgradeResolved)
	{
		m_upgrade = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(data->m_requiredUpgrade) : nullptr;
		m_upgradeResolved = TRUE;
		DEBUG_ASSERTCRASH(m_upgrade != nullptr, ("AoCTimedDropUpdate: RequiredUpgrade '%s' is not an upgrade; the drop never arms",
			data->m_requiredUpgrade.str()));
	}
	if (m_upgrade == nullptr)
		return FALSE;
	const Object *obj = getObject();
	if (m_upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER)
	{
		const Player *owner = obj->getControllingPlayer();
		return owner != nullptr && owner->hasUpgradeComplete(m_upgrade);
	}
	return obj->hasUpgrade(m_upgrade);
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCTimedDropUpdate::drawDelay() const
{
	const AoCTimedDropUpdateModuleData *data = getAoCTimedDropUpdateModuleData();
	if (data->m_minDelay == data->m_maxDelay)
		return data->m_minDelay;	// a fixed delay draws nothing (stock OCLUpdate always draws)
	return (UnsignedInt)GameLogicRandomValue((Int)data->m_minDelay, (Int)data->m_maxDelay);
}

// ------------------------------------------------------------------------------------------------
UpdateSleepTime AoCTimedDropUpdate::update()
{
	const AoCTimedDropUpdateModuleData *data = getAoCTimedDropUpdateModuleData();
	Object *obj = getObject();
	const UnsignedInt now = TheGameLogic->getFrame();

	if (!m_state.armed)
	{
		if (!AoCTimedDrop::arm( m_state, hasRequiredUpgrade() ))
			return UPDATE_SLEEP( AoCTimedDrop::pollFrames() );
		dropLog( obj, "armed %s object %u", data->m_requiredUpgrade.isEmpty() ? "(none)" : data->m_requiredUpgrade.str(), (UnsignedInt)obj->getID() );
	}

	const Bool underConstruction = obj->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION );
	const Bool disabled = obj->isDisabled();
	const UnsignedInt delay = AoCTimedDrop::needsDelay( m_state, now, underConstruction, disabled ) ? drawDelay() : 0;
	if (AoCTimedDrop::armedUpdate( m_state, now, underConstruction, disabled, delay ) == AOC_TIMED_DROP_DROP)
		drop();

	return UPDATE_SLEEP_NONE;	// as OCLUpdate: the disabled shift needs every frame
}

// ------------------------------------------------------------------------------------------------
void AoCTimedDropUpdate::drop()
{
	const AoCTimedDropUpdateModuleData *data = getAoCTimedDropUpdateModuleData();
	Object *obj = getObject();
	Coord3D creationCoord;
	if (data->m_createAtEdge)
		creationCoord = TheTerrainLogic->findClosestEdgePoint( obj->getPosition() );
	else
		creationCoord = *obj->getPosition();
	ObjectCreationList::create( data->m_ocl, obj, &creationCoord, obj->getPosition(), obj->getOrientation() );
	if (AoCHarness::isActive())
	{
		// the owner's bank at the drop, before the crates land: the next drop line shows what they paid
		char bank[256];
		AoCTrade::bankText( obj->getControllingPlayer(), bank, sizeof(bank) );
		dropLog( obj, "drop %s object %u next %u bank %s", data->m_oclName.str(), (UnsignedInt)obj->getID(), m_state.nextDropFrame, bank );
	}
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCTimedDropUpdate::getRemainingFrames() const
{
	return AoCTimedDrop::remainingFrames( m_state, TheGameLogic->getFrame() );
}

// ------------------------------------------------------------------------------------------------
Real AoCTimedDropUpdate::getCountdownPercent() const
{
	return AoCTimedDrop::countdownPercent( m_state, TheGameLogic->getFrame() );
}

// ------------------------------------------------------------------------------------------------
/*static*/ const AoCTimedDropUpdate *AoCTimedDropUpdate::findArmed( Object *obj )
{
	if (obj == nullptr)
		return nullptr;
	static const NameKeyType key = NAMEKEY( "AoCTimedDropUpdate" );
	const AoCTimedDropUpdate *update = (const AoCTimedDropUpdate *)obj->findUpdateModule( key );
	return (update && update->isArmed()) ? update : nullptr;
}

// ------------------------------------------------------------------------------------------------
// The chip (client)
// ------------------------------------------------------------------------------------------------
namespace
{
GameWindow *s_timerChip = nullptr;	///< the chip window as last looked up (the control bar layout lives for the session)

GameWindow *timerChipWindow()
{
	if (TheWindowManager == nullptr)
		return nullptr;
	static const NameKeyType key = NAMEKEY( "ControlBar.wnd:AoCTimerChip" );
	s_timerChip = TheWindowManager->winGetWindowFromId( nullptr, key );	// nullptr with the stock layout: nothing is drawn
	return s_timerChip;
}
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCTimedDropUpdate::updateTimerChip( Object *selected )
{
	// Called every frame in the command context: the window found by the last context switch (hideTimerChip), no search.
	GameWindow *chip = s_timerChip;
	if (chip == nullptr)
		return;
	const AoCTimedDropUpdate *drop = findArmed( selected );
	if (drop == nullptr || drop->m_state.nextDropFrame == 0)
	{
		if (!chip->winIsHidden())
			chip->winHide( TRUE );
		return;
	}
	const UnicodeString text = AoCTimedDrop::chipText( drop->getRemainingFrames() );
	if (GadgetStaticTextGetText( chip ).compare( text ) != 0)
		GadgetStaticTextSetText( chip, text );
	if (chip->winIsHidden())
		chip->winHide( FALSE );
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCTimedDropUpdate::hideTimerChip()
{
	GameWindow *chip = timerChipWindow();
	if (chip && !chip->winIsHidden())
		chip->winHide( TRUE );
}

// ------------------------------------------------------------------------------------------------
/*static*/ UnicodeString AoCTimedDropUpdate::shownChipText()
{
	GameWindow *chip = timerChipWindow();
	if (chip == nullptr || chip->winIsHidden())
		return UnicodeString::TheEmptyString;
	return GadgetStaticTextGetText( chip );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AoCTimedDropUpdate::crc( Xfer *xfer )
{
	// extend base class
	UpdateModule::crc( xfer );

	m_state.crc( xfer );
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version: the next-drop frame, the timer-start frame, armed (D11 F11) */
// ------------------------------------------------------------------------------------------------
void AoCTimedDropUpdate::xfer( Xfer *xfer )
{
	// version
	const XferVersion currentVersion = AoCTimedDropState::XFER_VERSION;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class (it saves the next call frame, so a sleeping poll resumes on load)
	UpdateModule::xfer( xfer );

	m_state.xfer( xfer, version );
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AoCTimedDropUpdate::loadPostProcess()
{
	// extend base class
	UpdateModule::loadPostProcess();
}
