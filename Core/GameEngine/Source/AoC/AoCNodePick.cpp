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

// AoCNodePick.cpp
// Age of Commanders F2b (design D11 section 2.2): resource nodes as mouse targets. See AoCNodePick.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCNodePick.h"
#include "AoC/AoCRules.h"

#include "Common/KindOf.h"
#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"
#include "GameClient/View.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"

// ------------------------------------------------------------------------------------------------
AoCNodePickContext::AoCNodePickContext() :
	rulesOn(FALSE),
	forceAttack(FALSE),
	forceMove(FALSE),
	waypoint(FALSE),
	attackMove(FALSE),
	guiCommand(FALSE),
	controllable(FALSE),
	hasGatherer(FALSE),
	stockTypes(0)
{
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCNodePick::extraPickTypes( const AoCNodePickContext &context )
{
	if (!context.rulesOn)
		return 0;
	// a mode or a GUI command decides the click itself (force attack fires at the tree, force move and waypoints go to
	// the ground, a GUI command picks by its own ALLOW_SHRUBBERY_TARGET option): the stock pick only
	if (context.forceAttack || context.forceMove || context.waypoint || context.attackMove || context.guiCommand)
		return 0;
	if (!context.controllable || !context.hasGatherer)
		return 0;
	// the stock types already pick trees (a flame weapon in the mouse-over pick): the stock pick decides
	if ((context.stockTypes & (UnsignedInt)PICK_TYPE_SHRUBBERY) != 0)
		return 0;
	return (UnsignedInt)PICK_TYPE_SHRUBBERY;
}

// ------------------------------------------------------------------------------------------------
AoCNodePickContext AoCNodePick::liveContext( Bool forceAttackMode, UnsignedInt stockTypes )
{
	AoCNodePickContext context;
	context.rulesOn = AoCRules::enabled();
	context.stockTypes = stockTypes;
	if (!context.rulesOn || TheInGameUI == nullptr)
		return context;
	context.forceAttack = forceAttackMode;
	context.forceMove = TheInGameUI->isInForceMoveToMode();
	context.waypoint = TheInGameUI->isInWaypointMode();
	context.attackMove = TheInGameUI->isInAttackMoveToMode();
	context.guiCommand = TheInGameUI->getGUICommand() != nullptr;
	context.controllable = TheInGameUI->areSelectedObjectsControllable();
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	if (selected)
	{
		for (DrawableListCIt it = selected->begin(); it != selected->end() && !context.hasGatherer; ++it)
		{
			const Object *obj = (*it) ? (*it)->getObject() : nullptr;
			const AIUpdateInterface *ai = obj ? obj->getAI() : nullptr;
			context.hasGatherer = ai != nullptr && ai->getSupplyTruckAIInterface() != nullptr;
		}
	}
	return context;
}

// ------------------------------------------------------------------------------------------------
Drawable *AoCNodePick::nodeOrStockPick( const ICoord2D *pixel, Bool forceAttackMode, UnsignedInt stockTypes, Drawable *stockPick )
{
	if (pixel == nullptr || TheTacticalView == nullptr || TheInGameUI == nullptr)
		return stockPick;
	const UnsignedInt extra = extraPickTypes(liveContext(forceAttackMode, stockTypes));
	if (extra == 0)
		return stockPick;
	// the same ray with trees added: the nearest drawable of the stock types or a tree, so a unit in front of the
	// tree still wins and a tree in front of a unit wins only when it is a node for the selection
	Drawable *draw = TheTacticalView->pickDrawable(pixel, forceAttackMode, (PickType)(stockTypes | extra));
	if (draw == nullptr || draw == stockPick)
		return stockPick;
	const Object *obj = draw->getObject();
	if (obj == nullptr || obj->isEffectivelyDead() || !obj->isKindOf(KINDOF_SHRUBBERY))
		return stockPick;
	// the check evaluateContextCommand makes before MSG_DOCK: every selected object can dock there (canDockAt, which
	// asks canTransferSuppliesAt: a node with units left, of a type the gatherer may carry, not an enemy's)
	if (!TheInGameUI->canSelectedObjectsDoAction(InGameUI::ACTIONTYPE_DOCK_AT, obj, InGameUI::SELECTION_ALL))
		return stockPick;
	return draw;
}
