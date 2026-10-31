/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: SupplyCenterDockUpdate.h /////////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood Feb 2002
// Desc:   The action of this dock update is taking boxes and turning them into money for my ownerplayer
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCRules.h"	// AoC @feature F2
#include "Common/Player.h"
#include "Common/Xfer.h"
#include "GameLogic/Module/SupplyCenterDockUpdate.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameClient/Color.h"
#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"
#include "GameClient/GameText.h"


// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SupplyCenterDockUpdateModuleData::SupplyCenterDockUpdateModuleData()
{
	m_grantTemporaryStealthFrames = 0;
	m_aocAcceptsMask = AoCGather::typeBit( AOC_RESOURCE_CASH );	// AoC @hook 31/10/2026 A drop-off takes cash unless AoCAccepts says otherwise (D11 F2).
	m_aocValuePerUnit = 1;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/*static*/ void SupplyCenterDockUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{

	DockUpdateModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "GrantTemporaryStealth",		INI::parseDurationUnsignedInt,  nullptr, offsetof( SupplyCenterDockUpdateModuleData, m_grantTemporaryStealthFrames ) },
		// AoC @hook 31/10/2026 The types this drop-off takes and what a unit of them is worth (D11 F2).
		{ "AoCAccepts",				AoCGather::parseResourceTypeList,  nullptr, offsetof( SupplyCenterDockUpdateModuleData, m_aocAcceptsMask ) },
		{ "AoCValuePerUnit",	INI::parseInt,  nullptr, offsetof( SupplyCenterDockUpdateModuleData, m_aocValuePerUnit ) },
		{ nullptr, nullptr, nullptr, 0 }
	};

  p.add(dataFieldParse);

}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SupplyCenterDockUpdate::SupplyCenterDockUpdate( Thing *thing, const ModuleData* moduleData ) : DockUpdate( thing, moduleData )
{
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
SupplyCenterDockUpdate::~SupplyCenterDockUpdate()
{
}

// TheSuperHackers @bugfix arcticdolphin 12/03/2026 Scale supply bonus proportionally to delivered boxes.
static UnsignedInt getUpgradedSupplyBoostValue( SupplyTruckAIInterface* supplyTruckAI )
{
#if RETAIL_COMPATIBLE_CRC
	return supplyTruckAI->getUpgradedSupplyBoost();
#else
	const Int maxBoxes = supplyTruckAI->getMaxBoxes();
	if (maxBoxes > 0)
	{
		const Int upgradedSupplyBoost = supplyTruckAI->getUpgradedSupplyBoost();
		const Int deliveredBoxes = supplyTruckAI->getNumberBoxes();
		return (upgradedSupplyBoost * deliveredBoxes) / maxBoxes;
	}
	return 0;
#endif
}

// ------------------------------------------------------------------------------------------------
// AoC @feature F2: floating text of a typed deposit, "+10 Wood": the type's DisplayName when it names a string,
// else the type name. Text only, for the stock local-player floating text block.
// ------------------------------------------------------------------------------------------------
static UnicodeString aocDepositText( Int type, UnsignedInt amount )
{
	UnicodeString label;
	Bool exists = FALSE;
	if( TheAoCRules && TheAoCRules->getDisplayLabel( type ).isNotEmpty() )
		label = TheGameText->fetch( TheAoCRules->getDisplayLabel( type ), &exists );
	if( !exists )
		label.translate( AsciiString( AoCGather::typeName( type ) ) );
	UnicodeString text;
	text.format( L"+%u ", amount );
	text.concat( label );
	return text;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool SupplyCenterDockUpdate::action( Object* docker, Object *drone )
{
	const SupplyCenterDockUpdateModuleData *data = getSupplyCenterDockUpdateModuleData();
	SupplyTruckAIInterface* supplyTruckAI = nullptr;
	if( docker->getAIUpdateInterface() == nullptr )
		return FALSE;

	supplyTruckAI = docker->getAIUpdateInterface()->getSupplyTruckAIInterface();

	DEBUG_ASSERTCRASH( supplyTruckAI != nullptr, ("Something Docking with a Supply Center must have a Supply-truck like AIUpdate") );
	if( supplyTruckAI == nullptr )
		return FALSE;

	UnsignedInt value = 0;

	// AoC @hook 31/10/2026 Typed cargo: a type this drop-off does not take is refused and kept (D11 F2).
	const Int aocType = supplyTruckAI->getCargoType();
	const Bool aocIsCash = (aocType == AOC_RESOURCE_CASH);
	const Int aocUnits = supplyTruckAI->getNumberBoxes();
	if( !aocAccepts( aocType ) )
	{
		AoCGather::log( docker->getID(), "refused %s %d at dropoff %u reason not-accepted", AoCGather::typeName( aocType ), aocUnits, (UnsignedInt)getObject()->getID() );
		return FALSE;
	}

	if( aocIsCash )	// AoC @hook 31/10/2026 The supply boost upgrades are a cash rule (D11 F2).
		value += getUpgradedSupplyBoostValue( supplyTruckAI );

	Player *ownerPlayer = getObject()->getControllingPlayer();
	while( supplyTruckAI->loseOneBox() )
		value += aocIsCash ? ownerPlayer->getSupplyBoxValue() : (UnsignedInt)data->m_aocValuePerUnit;	// AoC @hook 31/10/2026 Cash keeps the box value, other types AoCValuePerUnit (D11 F2).

	if( value > 0)
	{
		// AoC @hook 31/10/2026 Cash keeps the stock deposit and score; any other type goes into the bank (D11 F2).
		if( aocIsCash )
		{
			Money *ownerPlayerMoney = ownerPlayer->getMoney();
			ownerPlayerMoney->deposit(value);
			ownerPlayer->getScoreKeeper()->addMoneyEarned(value);
		}
		else
		{
			ownerPlayer->getAoCBank()->deposit( aocType, value );
		}


		if( data->m_grantTemporaryStealthFrames > 0 )
		{
			StealthUpdate *stealth = docker->getStealth();
			//Only grant temporary stealth to the default stealth update. It's
			//possible that another type of stealth was granted... like the
			//GPS scrambler. We want that to take precedence.
			if( getObject()->testStatus( OBJECT_STATUS_STEALTHED ) )
			{
				if( !stealth )
				{
					DEBUG_CRASH( ("SupplyCenterDockUpdate::action() -- It shouldn't be possible for a unit to be OBJECT_STATUS_STEALTHED without a StealthUpdate module!") );
				}
				else if( stealth->isTemporaryGrant() || !docker->testStatus( OBJECT_STATUS_CAN_STEALTH ) )
				{
					stealth->receiveGrant( TRUE, data->m_grantTemporaryStealthFrames );
				}
			}
		}
	}

	// AoC @hook 31/10/2026 Gather log line for the deposit (D11 F2).
	if( aocUnits > 0 )
		AoCGather::log( docker->getID(), "deposited %s %d value %u dropoff %u", AoCGather::typeName( aocType ), aocUnits, value, (UnsignedInt)getObject()->getID() );

	if (value > 0 && getObject()->isLogicallyVisible())
	{
		// OY LOOK!  I AM USING LOCAL PLAYER.  Do not put anything other than TheInGameUI->addFloatingText in the block this controls!!!
		// Setup info for adding a floating text
		Coord3D pos;
		const Coord3D *dockerPos;
		UnicodeString moneys;
		if( aocIsCash )
			moneys.format( TheGameText->fetch( "GUI:AddCash" ), value );
		else
			moneys = aocDepositText( aocType, value );	// AoC @hook 31/10/2026 The type label in the floating text (D11 F2).
		dockerPos = docker->getPosition();
		pos.x = dockerPos->x;
		pos.y = dockerPos->y;
		pos.z = TheTerrainLogic->getGroundHeight(pos.x, pos.y);//dockerPos->z + docker->getGeometryInfo().getHeight();
		Color color = ownerPlayer->getPlayerColor() | GameMakeColor( 0, 0, 0, 230 );

		TheInGameUI->addFloatingText(moneys, &pos, color);
	}


	return FALSE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
UpdateSleepTime SupplyCenterDockUpdate::update()
{
	//extend
	UpdateSleepTime result = DockUpdate::update();

#ifdef _DEBUG_ECONOMY
	static const NameKeyType key_SupplyCenterCreate = NAMEKEY("SupplyCenterCreate");
	SupplyCenterCreate* create = (SupplyCenterCreate*)getObject()->findCreateModule(key_SupplyCenterCreate);
	DEBUG_ASSERTCRASH( create && ! create->shouldDoOnBuildComplete(), ("A Supply center did not call onBuildComplete.") );
#endif

	return result;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void SupplyCenterDockUpdate::crc( Xfer *xfer )
{

	// extend base class
	DockUpdate::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void SupplyCenterDockUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	DockUpdate::xfer( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void SupplyCenterDockUpdate::loadPostProcess()
{

	// extend base class
	DockUpdate::loadPostProcess();

}
