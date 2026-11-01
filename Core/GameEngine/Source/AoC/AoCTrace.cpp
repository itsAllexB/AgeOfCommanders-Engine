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

// AoCTrace.cpp
// Age of Commanders F12b: the pure part of -crcEvery, -crcWindow and -aocRandomTrace (see AoCTrace.h).

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCTrace.h"

#include <stdio.h>
#include <string.h>

namespace
{

Int finish( int written, size_t size )
{
	if (written < 0 || (size_t)written >= size)
		return 0;
	return (Int)written;
}

}

namespace AoCTrace
{

Bool inWindow( UnsignedInt from, UnsignedInt to, UnsignedInt frame )
{
	return to != 0 && frame >= from && frame <= to;
}

Bool crcLineDue( const CrcSchedule &schedule, UnsignedInt frame, Bool engineComputed )
{
	if (inWindow(schedule.windowFrom, schedule.windowTo, frame))
		return TRUE;
	if (schedule.every != 0)
		return (frame % schedule.every) == 0 && (frame != 0 || engineComputed);	// frame 0 only where the engine computed it
	return engineComputed;
}

Bool hasSeedColumn( const CrcSchedule &schedule )
{
	return schedule.every != 0 || schedule.windowTo != 0;
}

Int formatCrcLine( char *buf, size_t size, const CrcSchedule &schedule, UnsignedInt frame, UnsignedInt crc, UnsignedInt seed )
{
	if (buf == nullptr || size == 0)
		return 0;
	const int n = hasSeedColumn(schedule)
		? snprintf(buf, size, "%u,%08X,%08X\n", frame, crc, seed)
		: snprintf(buf, size, "%u,%08X\n", frame, crc);
	const Int len = finish(n, size);
	if (len == 0)
		buf[0] = '\0';
	return len;
}

Bool parseCount( const char *text, UnsignedInt &out )
{
	if (text == nullptr || *text == '\0')
		return FALSE;
	unsigned long long value = 0;
	for (const char *p = text; *p != '\0'; ++p)
	{
		if (*p < '0' || *p > '9')
			return FALSE;
		value = value * 10 + (unsigned long long)(*p - '0');
		if (value > 4000000000ULL)
			return FALSE;
	}
	if (value == 0)
		return FALSE;
	out = (UnsignedInt)value;
	return TRUE;
}

Bool parseWindow( const char *fromText, const char *toText, UnsignedInt &from, UnsignedInt &to )
{
	UnsignedInt a = 0;
	UnsignedInt b = 0;
	if (!parseCount(fromText, a) || !parseCount(toText, b) || b < a)
		return FALSE;
	from = a;
	to = b;
	return TRUE;
}

const char *baseName( const char *path )
{
	if (path == nullptr)
		return "";
	const char *base = path;
	for (const char *p = path; *p != '\0'; ++p)
	{
		if (*p == '/' || *p == '\\')
			base = p + 1;
	}
	return base;
}

Int formatRandomInt( char *buf, size_t size, UnsignedInt frame, Bool inLogicUpdate, char kind, const char *file, Int line, Int lo, Int hi, Int value )
{
	if (buf == nullptr || size == 0)
		return 0;
	const int n = snprintf(buf, size, "%u,%c,%c,%s:%d,%d,%d,%d\n", frame, inLogicUpdate ? 'L' : 'C', kind, baseName(file), line, lo, hi, value);
	const Int len = finish(n, size);
	if (len == 0)
		buf[0] = '\0';
	return len;
}

Int formatRandomReal( char *buf, size_t size, UnsignedInt frame, Bool inLogicUpdate, char kind, const char *file, Int line, Real lo, Real hi, Real value )
{
	if (buf == nullptr || size == 0)
		return 0;
	const int n = snprintf(buf, size, "%u,%c,%c,%s:%d,%.9g,%.9g,%.9g\n", frame, inLogicUpdate ? 'L' : 'C', kind, baseName(file), line, (double)lo, (double)hi, (double)value);
	const Int len = finish(n, size);
	if (len == 0)
		buf[0] = '\0';
	return len;
}

Int formatCrcPoint( char *buf, size_t size, UnsignedInt frame, UnsignedInt seed )
{
	if (buf == nullptr || size == 0)
		return 0;
	const int n = snprintf(buf, size, "#%u,crc-point,%08X\n", frame, seed);
	const Int len = finish(n, size);
	if (len == 0)
		buf[0] = '\0';
	return len;
}

}
