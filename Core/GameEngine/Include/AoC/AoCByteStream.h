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

// AoCByteStream.h
// Age of Commanders F14a: append plain values to a byte vector and read them back with a bounds check. The pathfinder's
// zone data (AoCSaveFidelity) is packed this way, so the save block holds it as one blob the load applies once every
// other block is in (design D11 F14).

#pragma once

#include <string.h>
#include <vector>

namespace AoCByteStream
{
	template <class T> inline void put( std::vector<unsigned char> &out, const T &value )
	{
		const unsigned char *p = (const unsigned char *)&value;
		out.insert( out.end(), p, p + sizeof(T) );
	}

	template <class T> inline void putArray( std::vector<unsigned char> &out, const T *values, size_t count )
	{
		if( count == 0 || values == nullptr )
			return;
		const unsigned char *p = (const unsigned char *)values;
		out.insert( out.end(), p, p + sizeof(T) * count );
	}

	/// FALSE (value untouched) when fewer than sizeof(T) bytes are left.
	template <class T> inline bool get( const unsigned char *&p, const unsigned char *end, T &value )
	{
		if( (size_t)(end - p) < sizeof(T) )
			return false;
		memcpy( &value, p, sizeof(T) );
		p += sizeof(T);
		return true;
	}

	template <class T> inline bool getArray( const unsigned char *&p, const unsigned char *end, T *values, size_t count )
	{
		if( (size_t)(end - p) < sizeof(T) * count )
			return false;
		if( count > 0 )
			memcpy( values, p, sizeof(T) * count );
		p += sizeof(T) * count;
		return true;
	}
}
