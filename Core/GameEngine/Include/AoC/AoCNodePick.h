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

// AoCNodePick.h
// Age of Commanders F2b (design D11 section 2.2, ticket F2b): resource nodes as mouse targets.
//
// Stock picks the drawable under the cursor with getPickTypesForContext, which holds PICK_TYPE_SHRUBBERY only for a
// GUI command that allows shrubbery targets or a flame weapon in force-attack mode. So a tree that is an F2 node (a
// SupplyWarehouseDockUpdate on a SHRUBBERY template) is never the clicked or moused-over drawable: a click on it is a
// click on the ground (a move order) and no dock cursor shows, although ActionManager::canTransferSuppliesAt calls it
// a dock for the gatherer.
//
// The fix is client only. Under the AoC rules, with no GUI command and no force-attack, force-move, waypoint or
// attack-move mode, and a controllable selection that holds a gatherer (an AI with the SupplyTruckAIInterface, which
// carries F2's typed cargo), the click pick (CommandTranslator, left and right click) and the mouse-over pick
// (SelectionTranslator, the cursor) pick once more with PICK_TYPE_SHRUBBERY added to the stock types. The stock pick
// itself is made first and unchanged. The second pick wins only when it hits a living SHRUBBERY object the whole
// selection can dock at (InGameUI::canSelectedObjectsDoAction(ACTIONTYPE_DOCK_AT, SELECTION_ALL), the check the
// context evaluation makes before MSG_DOCK, which ends in canTransferSuppliesAt); otherwise the stock pick stands.
// The node then takes the stock context path: MSG_DOCK on a click, MSG_DOCK_HINT and the dock cursor on a mouse-over,
// as a supply warehouse does. A tree that is not a node for the selection (no SupplyWarehouseDockUpdate, a type a
// selected gatherer cannot carry, an empty node, a selected dozer that is no gatherer) leaves the stock result: the
// ground under it, a move order and the move cursor. With no AoCResourceType defined nothing here runs a second
// pick, so the stock pick types and results are unchanged bit for bit.
//
// KINDOF_IGNORED_IN_GUI stays on node trees (they copy the stock tree's SHRUBBERY IMMOBILE IGNORED_IN_GUI): no site
// that reads it hides a node's dock cursor. InGameUI::createMouseoverHint leaves the moused-over drawable id empty for
// such an object (no hover outline or health box, which a tree should not have), and createCommandHint sets the dock
// cursor for MSG_DOCK_HINT without reading that id; the other sites concern selected objects (voice responses, the
// mob nexus, the multi-select panel), icons, score and the health box.

#pragma once

#include "Lib/BaseType.h"

class Drawable;
struct ICoord2D;

/// What the pick-type decision reads (the pure part; the unit tests fill it directly).
struct AoCNodePickContext
{
	Bool rulesOn;						///< AoCRules::enabled(): an AoCResourceType is defined
	Bool forceAttack;				///< InGameUI::isInForceAttackMode
	Bool forceMove;					///< InGameUI::isInForceMoveToMode
	Bool waypoint;					///< InGameUI::isInWaypointMode
	Bool attackMove;				///< InGameUI::isInAttackMoveToMode
	Bool guiCommand;				///< InGameUI::getGUICommand() != nullptr (the command's own options decide its pick)
	Bool controllable;			///< InGameUI::areSelectedObjectsControllable
	Bool hasGatherer;				///< a selected object's AI has the SupplyTruckAIInterface (F2 typed cargo)
	UnsignedInt stockTypes;	///< the stock pick types of that pick (getPickTypesForContext)

	AoCNodePickContext();
};

namespace AoCNodePick
{
	/// The pick types to add to the stock ones for a second pick that may find a node: PICK_TYPE_SHRUBBERY when the
	/// rules are on, no mode or GUI command overrides the context command, the selection is controllable and holds a
	/// gatherer, and the stock types do not already pick shrubbery; 0 (no second pick) otherwise.
	UnsignedInt extraPickTypes( const AoCNodePickContext &context );

	/// The client hook after a stock pick (CommandTranslator MSG_MOUSE_LEFT_CLICK and MSG_MOUSE_RIGHT_CLICK,
	/// SelectionTranslator mouse-over): returns the node under 'pixel' when extraPickTypes asks for a second pick and it
	/// hits a node the whole selection can dock at, else stockPick unchanged. forceAttackMode is the live mode, as the
	/// stock pick passes it to pickDrawable; stockTypes are the stock pick's types.
	Drawable *nodeOrStockPick( const ICoord2D *pixel, Bool forceAttackMode, UnsignedInt stockTypes, Drawable *stockPick );

	/// The decision's context from the live client state (TheInGameUI, the selection, the rules).
	AoCNodePickContext liveContext( Bool forceAttackMode, UnsignedInt stockTypes );
}
