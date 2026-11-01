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

// AoCCrcDump.cpp
// Age of Commanders F14a: the -crcDump harness flag's dump of the per-frame logic CRC (design D11 F12 and F14). See
// AoCCrcDump.h. Mirrors GameLogic::getCRC section by section; read only.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCCrcDump.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/ThingTemplate.h"
#include "Common/XferCRC.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/BodyModule.h"

#include <chrono>
#include <string>
#include <string.h>
#include <vector>

namespace
{
	/// The light CRC exactly as GameLogic::getCRC computes it. In builds with DEEP_CRC_TO_MEMORY (the GeneralsX default)
	/// getCRC uses XferDeepCRC, whose strings carry a length; elsewhere it uses XferCRC, whose strings do not.
	class DumpCRC : public XferCRC
	{
	public:
		DumpCRC() { open("AoCCrcDump"); }
#if DEEP_CRC_TO_MEMORY
		virtual void xferAsciiString( AsciiString *asciiStringData ) override
		{
			UnsignedShort len = (UnsignedShort)asciiStringData->getLength();
			xferUnsignedShort( &len );
			if( len > 0 )
				xferUser( (void *)asciiStringData->str(), sizeof( Byte ) * len );
		}
#endif
	};

	/// A DumpCRC that also keeps the bytes it was fed, for the hex lines of a full dump (which field moved).
	class RecordCRC : public DumpCRC
	{
	public:
		std::string hex() const
		{
			static const char digits[] = "0123456789ABCDEF";
			std::string s;
			s.reserve(m_bytes.size() * 2);
			for (size_t i = 0; i < m_bytes.size(); ++i)
			{
				s += digits[m_bytes[i] >> 4];
				s += digits[m_bytes[i] & 15];
			}
			return s;
		}
	protected:
		virtual void xferImplementation( void *data, Int dataSize ) override
		{
			if( data != nullptr && dataSize > 0 )
				m_bytes.insert( m_bytes.end(), (const UnsignedByte *)data, (const UnsignedByte *)data + dataSize );
			DumpCRC::xferImplementation( data, dataSize );
		}
	private:
		std::vector<UnsignedByte> m_bytes;
	};

	std::string bytesOf( Snapshot *snapshot )
	{
		RecordCRC c;
		c.xferSnapshot( snapshot );
		return c.hex();
	}

	UnsignedInt crcOf( Snapshot *snapshot )
	{
		DumpCRC c;
		c.xferSnapshot( snapshot );
		return c.getCRC();
	}

	UnsignedInt crcOfBytes( const void *data, Int size )
	{
		DumpCRC c;
		c.xferUser( const_cast<void *>(data), size );
		return c.getCRC();
	}

	UnsignedInt bitsOf( Real r )
	{
		UnsignedInt u;
		memcpy( &u, &r, sizeof(u) );
		return u;
	}

	void marker( Xfer &x, const char *text )
	{
		AsciiString m = text;
		x.xferAsciiString( &m );
	}

	// The four sections of GameLogic::getCRC, each into the given CRC (so one DumpCRC fed all four in order is the
	// frame's CRC).
	void objectsSection( Xfer &x )
	{
		marker( x, "MARKER:Objects" );
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
			x.xferSnapshot( obj );
		UnsignedInt seed = GetGameLogicRandomSeedCRC();
		if( x.getXferMode() == XFER_CRC )
			x.xferUnsignedInt( &seed );
	}

	void partitionSection( Xfer &x )
	{
		marker( x, "MARKER:ThePartitionManager" );
		x.xferSnapshot( ThePartitionManager );
	}

	void playersSection( Xfer &x )
	{
		marker( x, "MARKER:ThePlayerList" );
		x.xferSnapshot( ThePlayerList );
	}

	void aiSection( Xfer &x )
	{
		marker( x, "MARKER:TheAI" );
		x.xferSnapshot( TheAI );
	}

	UnsignedInt moduleCRCs( Object *obj )
	{
		DumpCRC c;
		for( BehaviorModule **b = obj->getBehaviorModules(); *b; ++b )
			c.xferSnapshot( *b );
		return c.getCRC();
	}

	void writeObject( FILE *fp, Object *obj )
	{
		DumpCRC wpn;
		for( Int i = 0; i < WEAPONSLOT_COUNT; ++i )
		{
			Weapon *w = obj->getWeaponInWeaponSlot( (WeaponSlotType)i );
			if( w )
				wpn.xferSnapshot( w );
		}
		ExperienceTracker *exp = obj->getExperienceTracker();
		const UpgradeMaskType &upg = obj->getObjectCompletedUpgradeMask();
		BodyModuleInterface *body = obj->getBodyModule();
		const Coord3D *pos = obj->getPosition();
		fprintf( fp, "obj %d %s crc %08X mtx %08X health %08X wpn %08X exp %08X upg %08X bonus %08X scalar %08X mods %08X pos %.4f,%.4f,%.4f\n",
			(Int)obj->getID(), obj->getTemplate()->getName().str(), crcOf( obj ),
			crcOfBytes( obj->getTransformMatrix(), sizeof(Matrix3D) ),
			body ? bitsOf( body->getHealth() ) : 0u,
			wpn.getCRC(), exp ? crcOf( exp ) : 0u, crcOfBytes( &upg, sizeof(upg) ),
			(UnsignedInt)obj->getWeaponBonusCondition(), body ? bitsOf( body->getDamageScalar() ) : 0u,
			moduleCRCs( obj ), pos->x, pos->y, pos->z );
	}

	/// The wall time of the module crc pass Object::crc makes in a mod game (decision 4 of F14a), against the whole objects
	/// section, each the mean of several passes over the live objects. Harness only: the clock never reaches the logic.
	void writeCost( FILE *fp )
	{
		enum { PASSES = 50 };
		Int objects = 0;
		Int modules = 0;
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		{
			++objects;
			for( BehaviorModule **b = obj->getBehaviorModules(); *b; ++b )
				++modules;
		}
		UnsignedInt sink = 0;
		const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
		for( Int p = 0; p < PASSES; ++p )
		{
			DumpCRC c;
			for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
				for( BehaviorModule **b = obj->getBehaviorModules(); *b; ++b )
					c.xferSnapshot( *b );
			sink ^= c.getCRC();
		}
		const std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
		for( Int p = 0; p < PASSES; ++p )
		{
			DumpCRC c;
			objectsSection( c );
			sink ^= c.getCRC();
		}
		const std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
		const double moduleMs = std::chrono::duration<double, std::milli>( t1 - t0 ).count() / PASSES;
		const double objectsMs = std::chrono::duration<double, std::milli>( t2 - t1 ).count() / PASSES;
		fprintf( fp, "cost modulecrc %.4f ms objectsection %.4f ms objects %d modules %d passes %d (sink %08X)\n",
			moduleMs, objectsMs, objects, modules, (Int)PASSES, sink );
	}

	/// The pathfinder's ground map, which no save holds and the load rebuilds: per row, a CRC of the cells' zones, and one
	/// of their types, flags and the units that hold them as a goal or a position (the pathfinder's own CRC covers none).
	void writePathMap( FILE *fp )
	{
		Pathfinder *pf = TheAI->pathfinder();
		const ICoord2D *hi = pf ? pf->getExtent() : nullptr;
		if( hi == nullptr )
			return;
		for( Int y = 0; y <= hi->y; ++y )
		{
			DumpCRC zones, state;
			for( Int x = 0; x <= hi->x; ++x )
			{
				PathfindCell *cell = pf->getCell( LAYER_GROUND, x, y );
				if( cell == nullptr )
					continue;
				UnsignedShort zone = (UnsignedShort)cell->getZone();
				zones.xferUnsignedShort( &zone );
				Int type = (Int)cell->getType();
				Int flags = (Int)cell->getFlags();
				ObjectID goal = cell->getGoalUnit();
				ObjectID pos = cell->getPosUnit();
				state.xferInt( &type );
				state.xferInt( &flags );
				state.xferObjectID( &goal );
				state.xferObjectID( &pos );
			}
			fprintf( fp, "pathrow %d zones %08X cells %08X\n", (Int)y, zones.getCRC(), state.getCRC() );
		}
	}

	void writeModules( FILE *fp, Object *obj )
	{
		Int index = 0;
		for( BehaviorModule **b = obj->getBehaviorModules(); *b; ++b, ++index )
		{
			const UnsignedInt c = crcOf( *b );
			if( c == 0 )
				continue;	// a crc function that writes nothing (most stock modules)
			fprintf( fp, "mod %d %d %s %s %08X %s\n", (Int)obj->getID(), index,
				TheNameKeyGenerator->keyToName( (*b)->getModuleNameKey() ).str(),
				TheNameKeyGenerator->keyToName( (*b)->getModuleTagNameKey() ).str(), c, bytesOf( *b ).c_str() );
		}
	}
}

void AoCCrcDump::write( FILE *fp, UnsignedInt frame, Bool frameHasCRC, UnsignedInt frameCRC, Bool full )
{
	if( fp == nullptr || TheGameLogic == nullptr || ThePlayerList == nullptr || ThePartitionManager == nullptr || TheAI == nullptr )
		return;

	DumpCRC total;
	objectsSection( total );
	partitionSection( total );
	playersSection( total );
	aiSection( total );

	DumpCRC objects, partition, players, ai;
	objectsSection( objects );
	partitionSection( partition );
	playersSection( players );
	aiSection( ai );

	if( frameHasCRC )
		fprintf( fp, "frame %u crc %08X recomputed %08X%s seed %08X\n", frame, frameCRC, total.getCRC(),
			frameCRC == total.getCRC() ? "" : " MISMATCH", GetGameLogicRandomSeedCRC() );
	else
		fprintf( fp, "frame %u crc - recomputed %08X seed %08X\n", frame, total.getCRC(), GetGameLogicRandomSeedCRC() );
	fprintf( fp, "sections objects %08X partition %08X players %08X ai %08X\n", objects.getCRC(), partition.getCRC(),
		players.getCRC(), ai.getCRC() );

	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		writeObject( fp, obj );

	if( full )
	{
		for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
			writeModules( fp, obj );

		for( Int i = 0; i < ThePlayerList->getPlayerCount(); ++i )
		{
			Player *p = ThePlayerList->getNthPlayer( i );
			if( p )
				fprintf( fp, "player %d %08X %s\n", (Int)i, crcOf( p ), bytesOf( p ).c_str() );
		}

		const Int cx = ThePartitionManager->getCellCountX();
		const Int cy = ThePartitionManager->getCellCountY();
		for( Int y = 0; y < cy; ++y )
		{
			DumpCRC row;
			for( Int x = 0; x < cx; ++x )
			{
				PartitionCell *cell = ThePartitionManager->getCellAt( x, y );
				if( cell )
					row.xferSnapshot( cell );
			}
			fprintf( fp, "cellrow %d %08X\n", (Int)y, row.getCRC() );
		}
		for( Int y = 0; y < cy; ++y )
			for( Int x = 0; x < cx; ++x )
			{
				PartitionCell *cell = ThePartitionManager->getCellAt( x, y );
				if( cell )
					fprintf( fp, "cell %d %d %s\n", (Int)x, (Int)y, bytesOf( cell ).c_str() );
			}

		const std::list<AIGroup *> &groups = TheAI->getAoCGroupList();
		const TAiData *aiData = TheAI->getAiData();
		DumpCRC aiDataCRC;
		for( TAiData *d = const_cast<TAiData *>(aiData); d; d = d->m_next )
			aiDataCRC.xferSnapshot( d );
		fprintf( fp, "ai pathfinder %08X aidata %08X groups %d lastgroupid %u\n", crcOf( TheAI->pathfinder() ),
			aiDataCRC.getCRC(), (Int)groups.size(), TheAI->getAoCLastGroupID() );
		fprintf( fp, "pathfinder %s\n", bytesOf( TheAI->pathfinder() ).c_str() );
		writePathMap( fp );
		for( std::list<AIGroup *>::const_iterator it = groups.begin(); it != groups.end(); ++it )
		{
			if( *it == nullptr )
				continue;
			fprintf( fp, "group %u size %d crc %08X %s\n", (*it)->getID(), (*it)->getCount(), crcOf( *it ), bytesOf( *it ).c_str() );
		}

		writeCost( fp );
	}
	fflush( fp );
}
