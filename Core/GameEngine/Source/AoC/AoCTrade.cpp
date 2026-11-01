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

// AoCTrade.cpp
// Age of Commanders F11: the trade templates, their store and the AoCTrade INI block. See AoCTrade.h. The queue entry
// itself lives in ProductionUpdate (PRODUCTION_AOC_TRADE); this file holds what it and the control bar share.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCTrade.h"

#include "AoC/AoCFolder.h"
#include "AoC/AoCGather.h"
#include "AoC/AoCHarness.h"
#include "AoC/AoCResourceBank.h"
#include "AoC/AoCRules.h"
#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/MessageStream.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Eva.h"
#include "GameClient/GameText.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Module/ProductionUpdate.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

AoCTradeStore *TheAoCTradeStore = nullptr;

// ------------------------------------------------------------------------------------------------
// The template
// ------------------------------------------------------------------------------------------------
AoCTradeTemplate::AoCTradeTemplate()
{
	m_id = -1;
	m_direction = -1;
	m_deliveryMsecs = 0;
	m_deliveryFrames = 0;
	m_hasCost = FALSE;
	m_hasPayout = FALSE;
	m_resolved = FALSE;
	m_ocl = nullptr;
}

// ------------------------------------------------------------------------------------------------
/*static*/ const FieldParse *AoCTradeTemplate::getFieldParse()
{
	// The procedures write through 'instance' (the template being read); the offsets are unused.
	static const FieldParse fieldParse[] =
	{
		{ "Direction",			AoCTradeTemplate::parseDirection,				nullptr, 0 },
		{ "Cost",						AoCTradeTemplate::parseCost,						nullptr, 0 },
		{ "Payout",					AoCTradeTemplate::parsePayout,					nullptr, 0 },
		{ "DeliveryTime",		AoCTradeTemplate::parseDeliveryTime,		nullptr, 0 },
		{ "RequiresObject",	AoCTradeTemplate::parseRequiresObject,	nullptr, 0 },
		{ "DeliveryOCL",		INI::parseAsciiString,									nullptr, offsetof( AoCTradeTemplate, m_deliveryOCL ) },
		{ "DeliverySound",	INI::parseAsciiString,									nullptr, offsetof( AoCTradeTemplate, m_deliverySound ) },
		{ "ButtonImage",		INI::parseAsciiString,									nullptr, offsetof( AoCTradeTemplate, m_buttonImage ) },
		{ "DisplayName",		INI::parseAsciiString,									nullptr, offsetof( AoCTradeTemplate, m_displayLabel ) },
		{ "Description",		INI::parseAsciiString,									nullptr, offsetof( AoCTradeTemplate, m_descriptionLabel ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return fieldParse;
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCTradeTemplate::parseDirection( INI * /*ini*/, void *instance, void * /*store*/, const void * /*userData*/ )
{
	const char *token = INI::getNextToken();
	AoCTradeTemplate *self = (AoCTradeTemplate *)instance;
	if (stricmp(token, "Buy") == 0)
		self->m_direction = AOC_TRADE_BUY;
	else if (stricmp(token, "Sell") == 0)
		self->m_direction = AOC_TRADE_SELL;
	else
	{
		DEBUG_CRASH(("AoCTrade: Direction must be Buy or Sell, not '%s'", token));
		throw INI_INVALID_DATA;
	}
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCTradeTemplate::parseCost( INI *ini, void *instance, void * /*store*/, const void * /*userData*/ )
{
	AoCTradeTemplate *self = (AoCTradeTemplate *)instance;
	AoCCost::parseFromINI(ini, self->m_cost);	// an unknown type name throws INI_INVALID_DATA
	self->m_hasCost = TRUE;
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCTradeTemplate::parsePayout( INI *ini, void *instance, void * /*store*/, const void * /*userData*/ )
{
	AoCTradeTemplate *self = (AoCTradeTemplate *)instance;
	AoCCost::parseFromINI(ini, self->m_payout);
	self->m_hasPayout = TRUE;
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCTradeTemplate::parseDeliveryTime( INI * /*ini*/, void *instance, void * /*store*/, const void * /*userData*/ )
{
	// Milliseconds as written; frames are fixed in finish() at the live rate (set before any INI loads, F15).
	AoCTradeTemplate *self = (AoCTradeTemplate *)instance;
	const char *token = INI::getNextToken();
	if (token[0] == '-')
		throw INI_INVALID_DATA;
	self->m_deliveryMsecs = INI::scanUnsignedInt(token);
	if (self->m_deliveryMsecs == 0)
	{
		DEBUG_CRASH(("AoCTrade: DeliveryTime must be at least 1 ms"));
		throw INI_INVALID_DATA;
	}
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCTradeTemplate::parseRequiresObject( INI * /*ini*/, void *instance, void * /*store*/, const void * /*userData*/ )
{
	// Template names, any of which unlocks the trade; a second line adds to the list.
	AoCTradeTemplate *self = (AoCTradeTemplate *)instance;
	Int added = 0;
	for (const char *token = INI::getNextTokenOrNull(); token != nullptr; token = INI::getNextTokenOrNull())
	{
		self->m_requiresObject.push_back(AsciiString(token));
		++added;
	}
	if (added == 0)
	{
		DEBUG_CRASH(("AoCTrade: RequiresObject needs at least one template name"));
		throw INI_INVALID_DATA;
	}
	self->m_resolved = FALSE;
}

// ------------------------------------------------------------------------------------------------
void AoCTradeTemplate::finish()
{
	if (m_direction < 0)
	{
		DEBUG_CRASH(("AoCTrade %s: Direction (Buy or Sell) is required", m_name.str()));
		throw INI_INVALID_DATA;
	}
	if (!m_hasCost || m_cost.isZero())
	{
		DEBUG_CRASH(("AoCTrade %s: a Cost that is not zero is required", m_name.str()));
		throw INI_INVALID_DATA;
	}
	if (!m_hasPayout || m_payout.isZero())
	{
		DEBUG_CRASH(("AoCTrade %s: a Payout that is not zero is required", m_name.str()));
		throw INI_INVALID_DATA;
	}
	if (m_deliveryMsecs == 0)
	{
		DEBUG_CRASH(("AoCTrade %s: DeliveryTime is required", m_name.str()));
		throw INI_INVALID_DATA;
	}
	m_deliveryFrames = AoCTrade::deliveryFrames(m_deliveryMsecs);
	m_resolved = FALSE;
}

// ------------------------------------------------------------------------------------------------
const std::vector<const ThingTemplate *> &AoCTradeTemplate::getRequiredTemplates() const
{
	if (!m_resolved && TheThingFactory != nullptr)
	{
		m_requiredTemplates.clear();
		for (size_t i = 0; i < m_requiresObject.size(); ++i)
		{
			const ThingTemplate *tmpl = TheThingFactory->findTemplate(m_requiresObject[i]);
			if (tmpl)
				m_requiredTemplates.push_back(tmpl);
		}
		m_ocl = (m_deliveryOCL.isNotEmpty() && TheObjectCreationListStore) ?
			TheObjectCreationListStore->findObjectCreationList(m_deliveryOCL.str()) : nullptr;
		m_resolved = TRUE;
	}
	return m_requiredTemplates;
}

// ------------------------------------------------------------------------------------------------
const ObjectCreationList *AoCTradeTemplate::getDeliveryOCL() const
{
	getRequiredTemplates();	// resolves both
	return m_ocl;
}

// ------------------------------------------------------------------------------------------------
// The store
// ------------------------------------------------------------------------------------------------
AoCTradeStore::AoCTradeStore()
{
}

// ------------------------------------------------------------------------------------------------
AoCTradeStore::~AoCTradeStore()
{
	clear();
}

// ------------------------------------------------------------------------------------------------
void AoCTradeStore::clear()
{
	for (size_t i = 0; i < m_trades.size(); ++i)
		delete m_trades[i];
	m_trades.clear();
}

// ------------------------------------------------------------------------------------------------
void AoCTradeStore::loadDefinitions( Xfer *pXferCRC )
{
	// Both locations are optional (stock data has neither), so the "expect a file" flag is dropped. Through the INI CRC,
	// so peers with different trades are told apart in the lobby like any other INI difference.
	INI ini;
	ini.loadFileDirectory("Data\\INI\\Default\\AoCTrades", INI_LOAD_OVERWRITE, pXferCRC, INI::LoadFlags_SearchSubDirs);
	ini.loadFileDirectory("Data\\INI\\AoCTrades", INI_LOAD_OVERWRITE, pXferCRC, INI::LoadFlags_SearchSubDirs);
}

// ------------------------------------------------------------------------------------------------
const AoCTradeTemplate *AoCTradeStore::getNth( Int index ) const
{
	return (index >= 0 && index < (Int)m_trades.size()) ? m_trades[index] : nullptr;
}

// ------------------------------------------------------------------------------------------------
const AoCTradeTemplate *AoCTradeStore::findTrade( const char *name ) const
{
	if (name == nullptr)
		return nullptr;
	for (size_t i = 0; i < m_trades.size(); ++i)
		if (m_trades[i]->m_name.compareNoCase(name) == 0)
			return m_trades[i];
	return nullptr;
}

// ------------------------------------------------------------------------------------------------
const AoCTradeTemplate *AoCTradeStore::findTradeByID( Int id ) const
{
	return getNth(id);	// ids are load-order indices
}

// ------------------------------------------------------------------------------------------------
const AoCTradeTemplate *AoCTradeStore::defineTrade( const AsciiString &name, const AoCTradeTemplate &data )
{
	if (name.isEmpty())
		throw INI_INVALID_DATA;
	AoCTradeTemplate *trade = const_cast<AoCTradeTemplate *>(findTrade(name.str()));
	if (trade == nullptr)
	{
		trade = new AoCTradeTemplate;
		trade->m_id = (Int)m_trades.size();
		m_trades.push_back(trade);
	}
	const Int id = trade->m_id;
	*trade = data;
	trade->m_id = id;
	trade->m_name = name;
	trade->m_resolved = FALSE;
	trade->m_requiredTemplates.clear();
	trade->m_ocl = nullptr;
	return trade;
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCTradeStore::parseTradeDefinition( INI *ini )
{
	AsciiString name = ini->getNextToken();

	// Typed costs need the resource types (the feature flag, D11 1.6); refused before the fields are read, so the INI
	// error names the block header, as AoCConverterUpdate does.
	if (!AoCRules::enabled())
	{
		DEBUG_CRASH(("AoCTrade %s needs AoCResourceType definitions (Data\\INI\\AoCResources.ini)", name.str()));
		throw INI_INVALID_DATA;
	}

	AoCTradeTemplate scratch;
	scratch.m_name = name;
	ini->initFromINI(&scratch, AoCTradeTemplate::getFieldParse());

	// Trades are global and outlive the game, like the resource types, so a map's map.ini may not add one: the block is
	// consumed above and ignored on every machine.
	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		DEBUG_CRASH(("AoCTrade '%s' in '%s' ignored: trades are global and cannot be defined by a map", name.str(), ini->getFilename().str()));
		return;
	}

	scratch.finish();

	// Names that must exist once the templates and the creation lists are loaded (always, in the engine's load order;
	// the unit tests have neither store and skip the check).
	if (TheThingFactory)
	{
		for (size_t i = 0; i < scratch.m_requiresObject.size(); ++i)
		{
			if (TheThingFactory->findTemplate(scratch.m_requiresObject[i]) == nullptr)
			{
				DEBUG_CRASH(("AoCTrade %s: RequiresObject '%s' is not an object template", name.str(), scratch.m_requiresObject[i].str()));
				throw INI_INVALID_DATA;
			}
		}
	}
	if (TheObjectCreationListStore && scratch.m_deliveryOCL.isNotEmpty() &&
			TheObjectCreationListStore->findObjectCreationList(scratch.m_deliveryOCL.str()) == nullptr)
	{
		DEBUG_CRASH(("AoCTrade %s: DeliveryOCL '%s' is not an object creation list", name.str(), scratch.m_deliveryOCL.str()));
		throw INI_INVALID_DATA;
	}

	if (TheAoCTradeStore)
		TheAoCTradeStore->defineTrade(name, scratch);
}

// ------------------------------------------------------------------------------------------------
/*static*/ void AoCTradeStore::parseTradeReference( INI * /*ini*/, void * /*instance*/, void *store, const void * /*userData*/ )
{
	const char *token = INI::getNextToken();
	const AoCTradeTemplate **target = (const AoCTradeTemplate **)store;
	if (TheAoCTradeStore == nullptr)
	{
		// The base Generals game and the tools load no trades; the key is consumed and the button does nothing.
		*target = nullptr;
		return;
	}
	const AoCTradeTemplate *trade = TheAoCTradeStore->findTrade(token);
	if (trade == nullptr)
	{
		DEBUG_CRASH(("CommandButton AoCTrade = '%s': no such AoCTrade block (Data\\INI\\AoCTrades.ini)", token));
		throw INI_INVALID_DATA;
	}
	*target = trade;
}

// ------------------------------------------------------------------------------------------------
/*static*/ Int AoCTradeStore::count()
{
	return TheAoCTradeStore ? TheAoCTradeStore->getCount() : 0;
}

// ------------------------------------------------------------------------------------------------
/*static*/ const AoCTradeTemplate *AoCTradeStore::find( const char *name )
{
	return TheAoCTradeStore ? TheAoCTradeStore->findTrade(name) : nullptr;
}

// ------------------------------------------------------------------------------------------------
/*static*/ const AoCTradeTemplate *AoCTradeStore::findByID( Int id )
{
	return TheAoCTradeStore ? TheAoCTradeStore->findTradeByID(id) : nullptr;
}

// ------------------------------------------------------------------------------------------------
// Shared helpers
// ------------------------------------------------------------------------------------------------
AoCTradeRefusal AoCTrade::decide( const AoCTradeQueueFacts &facts )
{
	if (!facts.offered)
		return AOC_TRADE_REFUSED_NOT_OFFERED;
	if (!facts.requiresMet)
		return AOC_TRADE_REFUSED_REQUIRES;
	if (!facts.canAfford)
		return AOC_TRADE_REFUSED_FUNDS;
	if (facts.queueCount >= facts.queueMax)
		return AOC_TRADE_REFUSED_QUEUE_FULL;
	return AOC_TRADE_OK;
}

// ------------------------------------------------------------------------------------------------
const char *AoCTrade::refusalName( Int refusal )
{
	switch (refusal)
	{
		case AOC_TRADE_OK: return "ok";
		case AOC_TRADE_REFUSED_NOT_OFFERED: return "not-offered";
		case AOC_TRADE_REFUSED_REQUIRES: return "requires";
		case AOC_TRADE_REFUSED_FUNDS: return "funds";
		case AOC_TRADE_REFUSED_QUEUE_FULL: return "queue-full";
		default: return "?";
	}
}

// ------------------------------------------------------------------------------------------------
UnsignedInt AoCTrade::deliveryFrames( UnsignedInt msecs )
{
	const UnsignedInt frames = AoCGather::msecsToFrames(msecs);
	return frames > 0 ? frames : 1;
}

// ------------------------------------------------------------------------------------------------
Bool AoCTrade::requiresMet( const AoCTradeTemplate *trade, const Player *player )
{
	if (trade == nullptr)
		return FALSE;
	if (trade->getRequiredObjectNames().empty())
		return TRUE;
	const std::vector<const ThingTemplate *> &templates = trade->getRequiredTemplates();
	if (player == nullptr || templates.empty())
		return FALSE;
	std::vector<Int> counts(templates.size(), 0);
	// completed, living objects only: the steel mill must stand before steel can be bought (D2 section 6)
	player->countObjectsByThingTemplate((Int)templates.size(), &templates[0], TRUE, &counts[0], TRUE);
	for (size_t i = 0; i < counts.size(); ++i)
		if (counts[i] > 0)
			return TRUE;
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
namespace
{
Bool isTradeButtonFor( const CommandButton *button, const AoCTradeTemplate *trade )
{
	return button != nullptr && button->getCommandType() == GUI_COMMAND_AOC_TRADE && button->getAoCTrade() == trade;
}
}

// ------------------------------------------------------------------------------------------------
Bool AoCTrade::isOfferedBy( const Object *producer, const AoCTradeTemplate *trade )
{
	if (producer == nullptr || trade == nullptr || TheControlBar == nullptr)
		return FALSE;
	// CommandSets are a logic/client hybrid in the stock code too (Object::canProduceUpgrade reads them the same way).
	const CommandSet *set = TheControlBar->findCommandSet(producer->getCommandSetString());
	if (set == nullptr)
		return FALSE;
	for (Int i = 0; i < MAX_COMMANDS_PER_SET; ++i)
		if (isTradeButtonFor(set->getCommandButton(i), trade))
			return TRUE;
	// Buy > and Sell > are F5 folders: a trade button inside one is the building's own (one level, as for builds).
	for (Int i = 0; i < MAX_COMMANDS_PER_SET; ++i)
	{
		const CommandSet *folder = AoCFolder::folderSetOf(set->getCommandButton(i));
		if (folder == nullptr)
			continue;
		for (Int j = 0; j < MAX_COMMANDS_PER_SET; ++j)
			if (isTradeButtonFor(folder->getCommandButton(j), trade))
				return TRUE;
	}
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
void AoCTrade::vectorText( const AoCCost &cost, char *out, Int outSize )
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
void AoCTrade::bankText( const Player *player, char *out, Int outSize )
{
	if (out == nullptr || outSize <= 0)
		return;
	out[0] = '\0';
	Int used = 0;
	const Int types = AoCRules::typeCount();
	for (Int i = 0; i < types && i < AOC_MAX_RESOURCES; ++i)
	{
		const UnsignedInt amount = player ? player->getAoCBank()->count(i) : 0u;
		const int n = snprintf(out + used, (size_t)(outSize - used), "%s%s=%u", used > 0 ? " " : "", AoCGather::typeName(i), amount);
		if (n < 0 || used + n >= outSize)
			break;
		used += n;
	}
}

// ------------------------------------------------------------------------------------------------
Bool AoCTrade::logEnabled()
{
	return AoCHarness::isActive();
}

// ------------------------------------------------------------------------------------------------
void AoCTrade::log( const Player *player, const char *format, ... )
{
	if (!logEnabled())
		return;
	char text[768];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	text[sizeof(text) - 1] = '\0';
	fprintf(stderr, "[AoC trade] frame %u player %d %s\n", TheGameLogic ? TheGameLogic->getFrame() : 0u,
		player ? (int)player->getPlayerIndex() : -1, text);
	fflush(stderr);
}

// ------------------------------------------------------------------------------------------------
namespace
{
UnicodeString typeLabel( Int type )
{
	UnicodeString label;
	Bool exists = FALSE;
	if (TheAoCRules && TheGameText && TheAoCRules->getDisplayLabel(type).isNotEmpty())
		label = TheGameText->fetch(TheAoCRules->getDisplayLabel(type), &exists);
	if (!exists)
		label.translate(AsciiString(AoCGather::typeName(type)));
	return label;
}
}

// ------------------------------------------------------------------------------------------------
UnicodeString AoCTrade::deliveryText( const AoCCost &payout )
{
	UnicodeString text;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
	{
		if (payout.amount[i] == 0)
			continue;
		UnicodeString part;
		if (i == AOC_RESOURCE_CASH && TheGameText)
			part.format(TheGameText->fetch("GUI:AddCash"), payout.amount[i]);	// "+$150", the stock cash form
		else
			part = AoCGather::depositText(i, payout.amount[i]);								// "+300 Wood", F3's form
		if (!text.isEmpty())
			text.concat(L" ");
		text.concat(part);
	}
	return text;
}

// ------------------------------------------------------------------------------------------------
UnicodeString AoCTrade::vectorLabel( const AoCCost &cost )
{
	UnicodeString text;
	for (Int i = 0; i < AOC_MAX_RESOURCES; ++i)
	{
		if (cost.amount[i] == 0)
			continue;
		UnicodeString amount;
		amount.format(L"%u", cost.amount[i]);
		if (!text.isEmpty())
			text.concat(L", ");
		text.concat(typeLabel(i));
		text.concat(L" ");
		text.concat(amount);
	}
	return text;
}

// ------------------------------------------------------------------------------------------------
UnicodeString AoCTrade::requiresText( const AoCTradeTemplate *trade, const Player *player )
{
	UnicodeString text;
	if (trade == nullptr || requiresMet(trade, player))
		return text;
	const std::vector<const ThingTemplate *> &templates = trade->getRequiredTemplates();
	for (size_t i = 0; i < templates.size(); ++i)
	{
		if (!text.isEmpty())
		{
			text.concat(L" ");
			text.concat(TheGameText ? TheGameText->fetch("CONTROLBAR:OrRequirement") : UnicodeString(L"or"));
			text.concat(L" ");
		}
		text.concat(templates[i]->getDisplayName());
	}
	return text;
}

// ------------------------------------------------------------------------------------------------
// The hooks
// ------------------------------------------------------------------------------------------------
void AoCTrade::pressTradeButton( const CommandButton *button, Object *producer )
{
#if RTS_ZEROHOUR
	const AoCTradeTemplate *trade = button ? button->getAoCTrade() : nullptr;
	if (trade == nullptr || producer == nullptr || !producer->isLocallyControlled())
		return;
	ProductionUpdateInterface *pu = producer->getProductionUpdateInterface();
	if (pu == nullptr)
		return;

	const Int refusal = pu->canQueueAoCTrade(trade);
	if (refusal != AOC_TRADE_OK)
	{
		// The stock feedback of a refused click (GUI_COMMAND_UNIT_BUILD); nothing goes to the logic. The button is greyed while
		// RequiresObject is unmet, so that refusal only comes from a stale button, and says nothing.
		if (refusal == AOC_TRADE_REFUSED_FUNDS)
		{
			TheEva->setShouldPlay(EVA_InsufficientFunds);
			TheInGameUI->message("GUI:NotEnoughMoneyToBuild");
		}
		else if (refusal == AOC_TRADE_REFUSED_QUEUE_FULL)
		{
			TheInGameUI->message("GUI:ProductionQueueFull");
		}
		log(producer->getControllingPlayer(), "refused %s %s (click, nothing sent)", trade->getName().str(), refusalName(refusal));
		return;
	}

	// a production id from the producer, as the unit build click takes one, so the cancel message names the entry
	const ProductionID productionID = pu->requestUniqueUnitID();
	GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_AOC_QUEUE_TRADE);
	msg->appendIntegerArgument(trade->getID());
	msg->appendIntegerArgument((Int)productionID);
#else
	(void)button;
	(void)producer;
#endif
}

// ------------------------------------------------------------------------------------------------
void AoCTrade::queueFromMessage( Object *producer, Int tradeID, Int productionID )
{
#if RTS_ZEROHOUR
	const AoCTradeTemplate *trade = AoCTradeStore::findByID(tradeID);
	if (producer == nullptr || trade == nullptr)
		return;
	ProductionUpdateInterface *pu = producer->getProductionUpdateInterface();
	if (pu == nullptr)
		return;
	pu->queueAoCTrade(trade, (ProductionID)productionID);	// refuses (and logs) when the decision says no
#else
	(void)producer;
	(void)tradeID;
	(void)productionID;
#endif
}

// ------------------------------------------------------------------------------------------------
Int AoCTrade::borderFor( const CommandButton *button )
{
	const CommandButtonMappedBorderType border = button->getCommandButtonMappedBorderType();
	if (button->getCommandType() != GUI_COMMAND_AOC_TRADE || border != COMMAND_BUTTON_BORDER_NONE || button->getAoCTrade() == nullptr)
		return border;
	return button->getAoCTrade()->getDirection() == AOC_TRADE_SELL ? COMMAND_BUTTON_BORDER_ACTION : COMMAND_BUTTON_BORDER_BUILD;
}

// ------------------------------------------------------------------------------------------------
Bool AoCTrade::cancelsByProductionID( Int productionType )
{
#if RTS_ZEROHOUR
	return productionType == PRODUCTION_UNIT || productionType == PRODUCTION_AOC_TRADE;
#else
	return productionType == PRODUCTION_UNIT;
#endif
}
