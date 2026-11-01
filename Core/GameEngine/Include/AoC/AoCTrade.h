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

// AoCTrade.h
// Age of Commanders F11 (design D11 section 2.11, D2 section 6, D6 section 5): import trades. A trade is a queued
// shipment: its Cost is withdrawn when it is queued in a building's stock production queue, and its Payout is deposited
// when its delivery timer ends. Because it shares the stock queue, cancel and refund, the queue UI and the serialised
// cooldown (one shipment at a time per building, buying and selling alike) come with it.
//
//   AoCTrade AoCTestF11BuyWood
//     Direction      = Buy                          ; Buy | Sell: the UI and the AI only; required
//     Cost           = Cash:525                     ; an AoCCost, withdrawn at queue time; required, not zero
//     Payout         = Wood:300                     ; an AoCCost, deposited at delivery; required, not zero
//     DeliveryTime   = 120000                       ; ms, at least 1; frames at the live simulation rate (F15); required
//     RequiresObject = AmericaSteelMill GLAJunkYard ; optional: any one of them standing completed (the steel lock);
//                                                   ; several lines add to the list
//     DeliveryOCL    = OCL_AoCTestF11TradeCrate     ; optional, created at the building on delivery
//     DeliverySound  = EvaUSA_ReinforcementsHaveArrived ; optional audio event, for the local player only
//     ButtonImage    = SNSupplyTruck                ; the queue cameo
//     DisplayName    = CONTROLBAR:AoCBuyWood        ; label key (the tooltip's name line falls back to the button's)
//     Description    = CONTROLBAR:AoCTooltipBuyWood ; label key
//   End
//
//   CommandButton Command_AoCBuyWood
//     Command  = AOC_TRADE
//     AoCTrade = AoCTestF11BuyWood                  ; resolved when the button is parsed, like Upgrade
//     ...
//   End
//
// Load order: Data\INI\Default\AoCTrades(.ini and folder), then Data\INI\AoCTrades, after the object templates, the
// object creation lists and the upgrades (so RequiresObject and DeliveryOCL are checked against them) and before the
// command buttons (which name trades). Ids are given in load order, as template ids are, so every peer and replay agrees
// on them; the queue message carries the id. A second block with the same name redefines the trade in place (its id
// stays). A block read from a map's map.ini is consumed and ignored: trades are global, like the resource types. A trade
// needs resource types (TheAoCRules->isEnabled()); without them the block is an INI error, so stock data never has one.
//
// Prices are fixed. China's third-lot discount (D2 section 6: prices that move with volume) is later work.

#pragma once

#include "AoC/AoCCost.h"
#include "Common/AsciiString.h"
#include "Common/SubsystemInterface.h"
#include "Common/UnicodeString.h"

#include <vector>

struct FieldParse;
class CommandButton;
class INI;
class Object;
class ObjectCreationList;
class Player;
class ThingTemplate;
class Xfer;

enum AoCTradeDirection CPP_11(: Int)
{
	AOC_TRADE_BUY = 0,
	AOC_TRADE_SELL,
	AOC_TRADE_DIRECTION_COUNT
};

/// Why a trade cannot be queued. The order of the checks (AoCTrade::decide) is the order of the values.
enum AoCTradeRefusal CPP_11(: Int)
{
	AOC_TRADE_OK = 0,
	AOC_TRADE_REFUSED_NOT_OFFERED,		///< the producer has no AOC_TRADE button for it (its command set or one folder in it)
	AOC_TRADE_REFUSED_REQUIRES,				///< RequiresObject is unmet: no listed template stands completed
	AOC_TRADE_REFUSED_FUNDS,					///< the bank cannot afford the Cost
	AOC_TRADE_REFUSED_QUEUE_FULL,			///< the production queue is full
	AOC_TRADE_REFUSED_COUNT
};

/// The inputs of the queue decision, gathered by the producer (ProductionUpdate::canQueueAoCTrade) or a unit test.
struct AoCTradeQueueFacts
{
	Bool offered;
	Bool requiresMet;
	Bool canAfford;
	UnsignedInt queueCount;
	UnsignedInt queueMax;

	AoCTradeQueueFacts() : offered( TRUE ), requiresMet( TRUE ), canAfford( TRUE ), queueCount( 0 ), queueMax( 9 ) {}
};

//-------------------------------------------------------------------------------------------------
class AoCTradeTemplate
{
public:

	AoCTradeTemplate();

	const AsciiString &getName() const { return m_name; }
	Int getID() const { return m_id; }
	AoCTradeDirection getDirection() const { return (AoCTradeDirection)m_direction; }
	const AoCCost &getCost() const { return m_cost; }
	const AoCCost &getPayout() const { return m_payout; }
	UnsignedInt getDeliveryMsecs() const { return m_deliveryMsecs; }
	/// The delivery time in logic frames at the live rate (at least 1), fixed when the block was read.
	UnsignedInt getDeliveryFrames() const { return m_deliveryFrames; }
	const std::vector<AsciiString> &getRequiredObjectNames() const { return m_requiresObject; }
	const AsciiString &getDeliveryOCLName() const { return m_deliveryOCL; }
	const AsciiString &getDeliverySound() const { return m_deliverySound; }
	const AsciiString &getButtonImageName() const { return m_buttonImage; }
	const AsciiString &getDisplayLabel() const { return m_displayLabel; }
	const AsciiString &getDescriptionLabel() const { return m_descriptionLabel; }

	/// The RequiresObject templates that exist (resolved on first use; unknown names are left out).
	const std::vector<const ThingTemplate *> &getRequiredTemplates() const;
	/// The DeliveryOCL, nullptr when none is given or it does not exist.
	const ObjectCreationList *getDeliveryOCL() const;

	/// The block's fields, for INI::initFromINI on a scratch template (parseTradeDefinition) and the unit tests.
	static const FieldParse *getFieldParse();
	/// Checks a block once read: Direction, Cost, Payout and DeliveryTime given, Cost and Payout not zero; converts the
	/// delivery time to frames. Throws INI_INVALID_DATA.
	void finish();

	// field procedures (public for the unit tests)
	static void parseDirection( INI *ini, void *instance, void *store, const void *userData );
	static void parseCost( INI *ini, void *instance, void *store, const void *userData );
	static void parsePayout( INI *ini, void *instance, void *store, const void *userData );
	static void parseDeliveryTime( INI *ini, void *instance, void *store, const void *userData );
	static void parseRequiresObject( INI *ini, void *instance, void *store, const void *userData );

private:

	friend class AoCTradeStore;

	AsciiString m_name;
	Int m_id;
	Int m_direction;									///< AoCTradeDirection, -1 until parsed
	AoCCost m_cost;
	AoCCost m_payout;
	UnsignedInt m_deliveryMsecs;
	UnsignedInt m_deliveryFrames;
	std::vector<AsciiString> m_requiresObject;
	AsciiString m_deliveryOCL;
	AsciiString m_deliverySound;
	AsciiString m_buttonImage;
	AsciiString m_displayLabel;
	AsciiString m_descriptionLabel;
	Bool m_hasCost;
	Bool m_hasPayout;

	mutable Bool m_resolved;																///< m_requiredTemplates and m_ocl looked up
	mutable std::vector<const ThingTemplate *> m_requiredTemplates;
	mutable const ObjectCreationList *m_ocl;
};

//-------------------------------------------------------------------------------------------------
class AoCTradeStore : public SubsystemInterface
{
public:

	AoCTradeStore();
	virtual ~AoCTradeStore() override;

	virtual void init() override { }
	virtual void reset() override { }		///< trades are INI data and survive a game reset, like the templates
	virtual void update() override { }

	/// Load Data\INI\Default\AoCTrades(.ini and folder) and Data\INI\AoCTrades; both optional.
	void loadDefinitions( Xfer *pXferCRC );

	Int getCount() const { return (Int)m_trades.size(); }
	const AoCTradeTemplate *getNth( Int index ) const;
	const AoCTradeTemplate *findTrade( const char *name ) const;			///< case-insensitive; nullptr when unknown
	const AoCTradeTemplate *findTradeByID( Int id ) const;						///< nullptr when unknown

	/// Define or redefine a trade from a finished scratch template (INI blocks and unit tests); returns the stored one.
	const AoCTradeTemplate *defineTrade( const AsciiString &name, const AoCTradeTemplate &data );

	/// INI block parser for "AoCTrade <Name>" (registered in INI.cpp's block table).
	static void parseTradeDefinition( INI *ini );
	/// FieldParse procedure for the CommandButton key AoCTrade: 'store' points at a const AoCTradeTemplate *. An unknown
	/// name is INI_INVALID_DATA; without a store (the base Generals game, tools) the key is consumed and left empty.
	static void parseTradeReference( INI *ini, void *instance, void *store, const void *userData );

	// Helpers that tolerate a missing store.
	static Int count();
	static const AoCTradeTemplate *find( const char *name );
	static const AoCTradeTemplate *findByID( Int id );

private:

	void clear();

	std::vector<AoCTradeTemplate *> m_trades;
};

extern AoCTradeStore *TheAoCTradeStore;

//-------------------------------------------------------------------------------------------------
namespace AoCTrade
{
	/// The queue decision: not offered, then RequiresObject, then funds, then a full queue.
	AoCTradeRefusal decide( const AoCTradeQueueFacts &facts );
	/// "ok", "not-offered", "requires", "funds", "queue-full".
	const char *refusalName( Int refusal );

	/// Milliseconds to logic frames at the live rate (the INI duration rule, AoCGather::msecsToFrames), at least 1.
	UnsignedInt deliveryFrames( UnsignedInt msecs );

	/// TRUE when the trade has no RequiresObject, or the player owns a completed object of one of its templates.
	Bool requiresMet( const AoCTradeTemplate *trade, const Player *player );
	/// TRUE when the producer's command set, or a folder that set opens (AoCFolder, one level), holds an AOC_TRADE button
	/// for the trade. Command set data only, which every machine loads alike, like Object::canProduceUpgrade.
	Bool isOfferedBy( const Object *producer, const AoCTradeTemplate *trade );

	/// "Cash:525" (every non-zero component, "Name:amount", space-separated), "none" for a zero vector.
	void vectorText( const AoCCost &cost, char *out, Int outSize );
	/// "Cash=475 Food=0 Wood=300 ..." every defined type of the player's bank.
	void bankText( const Player *player, char *out, Int outSize );

	/// Evidence trail (ticket F11 decision 5): while any harness flag is given, "[AoC trade] frame F player N <event>".
	/// Nothing is printed in a run without flags; logic never reads it.
	Bool logEnabled();
	void log( const Player *player, const char *format, ... );

	// Client text (floating text, tooltips). Labels from the resource types (F6's keys) or the type names.
	/// "+300 Wood" per non-zero component, space-separated; cash as the stock "+$150" (GUI:AddCash).
	UnicodeString deliveryText( const AoCCost &payout );
	/// "Wood 300, Cash 150": the tooltip's cost line for a non-cash vector.
	UnicodeString vectorLabel( const AoCCost &cost );
	/// The unmet RequiresObject templates' display names joined with the stock "or" (CONTROLBAR:OrRequirement); empty
	/// when met or when there is no RequiresObject.
	UnicodeString requiresText( const AoCTradeTemplate *trade, const Player *player );

	// The hooks (Zero Hour; in the base Generals game they do nothing, as it has no trade entries).
	/// Client, ControlBar::processCommandUI on an AOC_TRADE button: the producer's queue decision for the local player, with
	/// the stock feedback of a refused click (EVA and "GUI:NotEnoughMoneyToBuild" for funds, "GUI:ProductionQueueFull"),
	/// else MSG_AOC_QUEUE_TRADE (trade id, a production id from requestUniqueUnitID, as GUI_COMMAND_UNIT_BUILD does).
	void pressTradeButton( const CommandButton *button, Object *producer );
	/// GameLogic::logicMessageDispatcher on MSG_AOC_QUEUE_TRADE: queue the trade at the one selected producer of the message's
	/// player (the stock rule for MSG_QUEUE_UNIT_CREATE); unknown ids and producers without a queue do nothing.
	void queueFromMessage( Object *producer, Int tradeID, Int productionID );
	/// The queue button's cancel (GUI_COMMAND_CANCEL_UNIT_BUILD) applies to units and shipments.
	Bool cancelsByProductionID( Int productionType );
}
