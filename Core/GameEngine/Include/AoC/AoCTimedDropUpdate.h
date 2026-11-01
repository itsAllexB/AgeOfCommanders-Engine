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

// AoCTimedDropUpdate.h
// Age of Commanders F11 (design D11 section 2.11, D2 section 6, D6 section 5): the timed drop behind the USA's Airlift
// Contract. It is the stock OCLUpdate's timer with an upgrade in front of it:
//
//   Behavior = AoCTimedDropUpdate ModuleTag_AoCDrop
//     OCL             = OCL_AmericaSupplyDropZoneCrateDrop ; created on each drop (required)
//     MinDelay        = 120000                             ; ms between drops, as OCLUpdate
//     MaxDelay        = 120000                             ; drawn through GameLogicRandomValue only when it differs
//     CreateAtEdge    = Yes                                ; at the map edge nearest the object, else on the object
//     RequiredUpgrade = Upgrade_AmericaAirliftContract     ; object or player upgrade; empty: armed at once
//   End
//
// Until the upgrade is there (Object::hasUpgrade for an object upgrade, Player::hasUpgradeComplete for a player one) the
// module sleeps one second at a time (frames through the F15 helper). Once it sees the upgrade it is armed for good and
// runs OCLUpdate::update's timer (the TheSuperHackers branch that keeps a subdued building's timer from running out while
// it is under construction): the first drop comes one delay after the timer starts, which is at arming, or when the
// building is complete if it is armed under construction; while the building is disabled the timer shifts by a frame per
// frame. The upgrade is checked only until arming.
//
// Its module name is not "OCLUpdate", so the control bar does not switch to the OCL timer context
// (ControlBar::evaluateContextUI looks a module up by that name key) and the building keeps its command set. The client
// shows the countdown in the chip window ControlBar.wnd:AoCTimerChip ("Next airdrop 1:42", GUI:AoCNextAirdrop) while the
// selected object has an armed module, and only when the loaded layout has that window (the stock one does not).
//
// The pure timer (AoCTimedDropState, AoCTimedDrop) is free of the object so the unit tests run it.

#pragma once

#include "GameLogic/Module/UpdateModule.h"

class Object;
class ObjectCreationList;
class UpgradeTemplate;
class Xfer;

/// The runtime fields, saved by the module's xfer and written into its crc.
struct AoCTimedDropState
{
	enum { XFER_VERSION = 1 };	///< the module's block version: 1, the three fields below

	UnsignedInt nextDropFrame;		///< OCLUpdate's m_nextCreationFrame: 0 until the timer first starts
	UnsignedInt timerStartFrame;	///< OCLUpdate's m_timerStartedFrame: 0 until the timer first starts
	Bool armed;										///< the RequiredUpgrade has been seen

	AoCTimedDropState();

	void xfer( Xfer *xfer, XferVersion version );
	void crc( Xfer *xfer );
};

/// What one armed update did.
enum AoCTimedDropStep CPP_11(: Int)
{
	AOC_TIMED_DROP_WAIT = 0,		///< too soon, or the object is under construction
	AOC_TIMED_DROP_SHIFTED,			///< disabled with the timer running: both frames moved on by one
	AOC_TIMED_DROP_STARTED,			///< the first start of the timer (no drop)
	AOC_TIMED_DROP_DROP,				///< the timer ran out: drop now, the timer restarted
};

namespace AoCTimedDrop
{
	/// The upgrade poll: arms the state when the upgrade is there. TRUE only on the call that arms it; once armed it stays
	/// armed, whatever the upgrade does later.
	Bool arm( AoCTimedDropState &state, Bool hasUpgrade );
	/// One armed update at frame 'now', OCLUpdate::update's timer: 'delay' is the delay a (re)started timer gets; the
	/// caller draws it only when needsDelay() says so, so the random draw happens exactly when OCLUpdate makes it.
	AoCTimedDropStep armedUpdate( AoCTimedDropState &state, UnsignedInt now, Bool underConstruction, Bool disabled, UnsignedInt delay );
	/// TRUE when armedUpdate at 'now' will (re)start the timer, i.e. needs a delay.
	Bool needsDelay( const AoCTimedDropState &state, UnsignedInt now, Bool underConstruction, Bool disabled );
	/// Frames to the next drop; 0 when the timer has not started or is due.
	UnsignedInt remainingFrames( const AoCTimedDropState &state, UnsignedInt now );
	/// 0 at the timer's start to 1 at the drop (OCLUpdate::getCountdownPercent); 0 before the timer starts.
	Real countdownPercent( const AoCTimedDropState &state, UnsignedInt now );
	/// The upgrade poll in frames: one second through the F15 helper (AoCGather::msecsToFrames), at least 1.
	UnsignedInt pollFrames();
	/// "Next airdrop 1:42": the chip text for a number of frames at the live rate (GUI:AoCNextAirdrop, "%d:%02d").
	UnicodeString chipText( UnsignedInt remainingFrames );
}

//-------------------------------------------------------------------------------------------------
class AoCTimedDropUpdateModuleData : public UpdateModuleData
{
public:
	AsciiString m_oclName;
	const ObjectCreationList *m_ocl;
	UnsignedInt m_minDelay;					///< frames
	UnsignedInt m_maxDelay;					///< frames
	Bool m_createAtEdge;
	AsciiString m_requiredUpgrade;	///< resolved at run time (upgrades load after the object templates)

	AoCTimedDropUpdateModuleData();

	static void buildFieldParse( MultiIniFieldParse &p );
	static void parseOCL( INI *ini, void *instance, void *store, const void *userData );
};

//-------------------------------------------------------------------------------------------------
class AoCTimedDropUpdate : public UpdateModule
{

	MEMORY_POOL_GLUE_WITH_EXPLICIT_CREATE( AoCTimedDropUpdate, "AoCTimedDropUpdate", 32, 32 )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( AoCTimedDropUpdate, AoCTimedDropUpdateModuleData )

public:

	AoCTimedDropUpdate( Thing *thing, const ModuleData *moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual UpdateSleepTime update() override;
	/// Every disable type, as OCLUpdate: the update shifts the timer while the object is disabled.
	virtual DisabledMaskType getDisabledTypesToProcess() const override { return DISABLEDMASK_ALL; }

	Bool isArmed() const { return m_state.armed; }
	UnsignedInt getRemainingFrames() const;		///< for the chip
	Real getCountdownPercent() const;					///< goes from 0 to 1, as OCLUpdate's

	/// The armed module of an object, or nullptr (the chip, the harness).
	static const AoCTimedDropUpdate *findArmed( Object *obj );

	/// Client: the chip for the selected object in the command context (ControlBar::updateContextCommand); hidden when
	/// the object has no armed module. Does nothing when the loaded layout has no ControlBar.wnd:AoCTimerChip.
	static void updateTimerChip( Object *selected );
	/// Client: hides the chip (ControlBar::switchToContext, before any context is shown).
	static void hideTimerChip();
	/// Client: the chip's text while it is shown, else empty (the harness log line).
	static UnicodeString shownChipText();

protected:

	Bool hasRequiredUpgrade() const;
	UnsignedInt drawDelay() const;
	void drop();

	AoCTimedDropState m_state;
	mutable const UpgradeTemplate *m_upgrade;		///< RequiredUpgrade, looked up on first use (not saved)
	mutable Bool m_upgradeResolved;

};
