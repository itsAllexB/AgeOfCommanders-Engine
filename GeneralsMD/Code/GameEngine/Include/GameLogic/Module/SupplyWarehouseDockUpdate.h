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

// FILE: SupplyWarehouseDockUpdate.h /////////////////////////////////////////////////////////////////////////////
// Author: Graham Smallwood Feb 2002
// Desc:   The action of this dock update is identifying who is docking and either taking Boxes away or giving them
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/INI.h"
#include "Common/GameMemory.h"
#include "GameLogic/Module/DockUpdate.h"

class SupplyTruckAIInterface;	// AoC @feature F2

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class SupplyWarehouseDockUpdateModuleData : public DockUpdateModuleData
{
public:

  SupplyWarehouseDockUpdateModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);

	Int m_startingBoxesData;
	Bool m_deleteWhenEmpty;
	Int m_aocResourceType;		///< AoC @feature F2: AoCResourceType, the node's type (default cash); template data, not saved
	Bool m_aocKillWhenEmpty;	///< AoC @feature F2: AoCKillWhenEmpty, kill() at zero so Die modules run (default No)
};

//-------------------------------------------------------------------------------------------------
class SupplyWarehouseDockUpdate : public DockUpdate
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( SupplyWarehouseDockUpdate, "SupplyWarehouseDockUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( SupplyWarehouseDockUpdate, SupplyWarehouseDockUpdateModuleData )

public:

	virtual DockUpdateInterface* getDockUpdateInterface() override { return this; }

	SupplyWarehouseDockUpdate( Thing *thing, const ModuleData* moduleData );

	virtual void setDockCrippled( Bool setting ) override; ///< Game Logic can set me as inoperative.  I get to decide what that means.
	virtual Bool action( Object* docker, Object *drone = nullptr ) override;	///<For me, this means identifying who is docking and either taking Boxes away or giving them

	Int getBoxesStored() const { return m_boxesStored; }

	void setCashValue( Int cashValue );

	virtual void onObjectCreated() override;

	// AoC @hook 31/10/2026 Typed nodes (D11 F2).
	Int getAoCResourceType() const { return getSupplyWarehouseDockUpdateModuleData()->m_aocResourceType; }
	/// The node type of an object with this module, -1 for any other object.
	static Int getAoCNodeTypeOf( const Object *obj );

protected:

	/// AoC F2: a type the docker cannot carry is refused; a load of another type is dropped first (D2 3.1). FALSE refuses.
	Bool aocPreparePickup( Object *docker, SupplyTruckAIInterface *ai );


	Int m_boxesStored;

};
