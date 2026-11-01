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

// AoCWanderAIUpdate.h
// Age of Commanders F9 (design D11 section 2.9): bounded wander, the AI module of ambient civilians and oxen.
// Derived the way the stock WanderAIUpdate is, which adds a random 5 to 50 to x and y whenever idle (so a walker
// drifts away and never returns) and updates every frame. This one strolls between random points around a fixed
// anchor and pauses between moves; the keys, the anchor rule and the picker are in AoCWander.h.
//
// Each update: on the first, fix the anchor. When the pause is over and the stock AI is idle, pick a point (up to
// AOC_WANDER_PICK_ATTEMPTS draws until the pathfinder's adjusted destination lies within one cell of the outer radius)
// and issue aiMoveToPosition (CMD_FROM_AI). Then the stock AI update runs. When the AI is idle again with a move in
// flight, the move has ended (arrived, failed, or replaced by stock behaviour that has since finished): draw a pause
// of MinPause to MaxPause. The module returns the shorter of its own sleep and the stock AI's: the pause while
// pausing, a quarter of MinPause while a move is in flight. A moving unit's stock AI asks for every frame (the
// locomotor), an idle one sleeps on its own scan interval, so a pausing walker costs nothing between its idle scans.
// Panic, repulsors, death and every other stock command stay stock AI behaviour.
//
// The module lives in Core and is compiled into both game targets: the base classes it uses (AIUpdateInterface,
// AIUpdateModuleData, the pathfinder) have the same interface in Generals and Zero Hour. Registered in both
// ModuleFactory.cpp copies; no stock template carries it.

#pragma once

#include "AoC/AoCWander.h"
#include "GameLogic/Module/AIUpdate.h"

class Object;

//-------------------------------------------------------------------------------------------------
class AoCWanderAIUpdateModuleData : public AIUpdateModuleData
{
public:
	AoCWanderData m_wander;	///< WanderRadius, MinPause, MaxPause, AnchorToProducer

	AoCWanderAIUpdateModuleData() {}

	static void buildFieldParse( MultiIniFieldParse &p );
};

//-------------------------------------------------------------------------------------------------
class AoCWanderAIUpdate : public AIUpdateInterface
{
	MEMORY_POOL_GLUE_WITH_EXPLICIT_CREATE( AoCWanderAIUpdate, "AoCWanderAIUpdate", 64, 64 )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( AoCWanderAIUpdate, AoCWanderAIUpdateModuleData )

public:

	AoCWanderAIUpdate( Thing *thing, const ModuleData *moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual UpdateSleepTime update() override;

	const AoCWanderState &getWanderState() const { return m_wander; }

	/// TRUE when the object's AI module is an AoCWanderAIUpdate (the -aiLog wanderers column).
	static Bool isWanderer( const Object *obj );

private:

	void fixAnchor();
	Bool startMove();

	AoCWanderState m_wander;
};
