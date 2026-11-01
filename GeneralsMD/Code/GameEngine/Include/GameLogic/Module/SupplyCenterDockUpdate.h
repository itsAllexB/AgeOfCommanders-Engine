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

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "AoC/AoCGather.h"	// AoC @feature F2
#include "Common/INI.h"
#include "Common/GameMemory.h"
#include "GameLogic/Module/DockUpdate.h"

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class SupplyCenterDockUpdateModuleData : public DockUpdateModuleData
{
public:
  SupplyCenterDockUpdateModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);

	UnsignedInt m_grantTemporaryStealthFrames;
	UnsignedInt m_aocAcceptsMask;	///< AoC @feature F2: AoCAccepts, one bit per type taken (default cash only); template data
	UnsignedInt m_aocValuePerUnit;	///< AoC @feature F2: AoCValuePerUnit, bank units per carried unit of every type but cash (default 1, at least 1)
};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class SupplyCenterDockUpdate : public DockUpdate
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( SupplyCenterDockUpdate, "SupplyCenterDockUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( SupplyCenterDockUpdate, SupplyCenterDockUpdateModuleData )

public:

	SupplyCenterDockUpdate( Thing *thing, const ModuleData* moduleData );

	virtual DockUpdateInterface* getDockUpdateInterface() override { return this; }
	virtual Bool action( Object* docker, Object *drone = nullptr ) override;	///<For me, this means taking boxes and turning them into money for my ownerplayer

	virtual UpdateSleepTime update() override;

	// AoC @hook 31/10/2026 Typed drop-offs (D11 F2).
	Bool aocAccepts( Int type ) const { return (getSupplyCenterDockUpdateModuleData()->m_aocAcceptsMask & AoCGather::typeBit( type )) != 0; }

protected:


};
