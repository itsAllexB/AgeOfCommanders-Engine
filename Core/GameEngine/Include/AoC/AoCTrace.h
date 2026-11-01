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

// AoCTrace.h
// Age of Commanders F12b (design D11 section 2.12, harness): the pure part of the per-frame determinism evidence,
// -crcEvery <n>, -crcWindow <from> <to> and -aocRandomTrace <from> <to> <file>, so the unit tests can run it.
// AoCHarness parses the flags, computes the CRCs and writes the files; this unit decides which frames get a line and
// what a line looks like.
//
// -crcLog lines. Without -crcEvery and -crcWindow a line is written at each frame the engine itself computes the CRC
// (every 100 logic frames in a skirmish or a replay), "frame,CRC", as since F1. With -crcEvery <n> a line is written at
// every frame that is a multiple of n instead, and with -crcWindow <from> <to> also at every frame from <from> to <to>
// (both included); the CRC of a frame the engine did not compute is computed at the same point of the logic update
// (GameLogic::getCRC, after the scripts and the terrain update, before the objects update). With either flag every line
// is "frame,CRC,SEED": SEED is GetGameLogicRandomSeedCRC, the logic random seed's CRC that the light CRC covers, so a
// diff shows whether the random stream parted before the state did.
//
// -aocRandomTrace lines, one per logic random draw (a call that advances the logic seed, or an ...Unchanged one) while
// the logic frame is from <from> to <to>: "frame,where,kind,file:line,lo,hi,value". where is L inside the logic update
// and C outside it (a draw from client code, which the logic must never take); kind is I (integer), R (real), and u or
// r for the ...Unchanged variants, which do not advance the seed in this build (RETAIL_COMPATIBLE_CRC 0); file is the
// caller's source file without its folders. A line "#frame,crc-point,SEED" marks the frame's CRC point, so the draws
// before it (the scripts) and after it (the objects) can be told apart.

#pragma once

#include "Lib/BaseType.h"

#include <stddef.h>

namespace AoCTrace
{
	/// When -crcLog writes: every (0 = only where the engine computes the CRC), and the window (windowTo 0 = none).
	struct CrcSchedule
	{
		UnsignedInt every;
		UnsignedInt windowFrom;
		UnsignedInt windowTo;
	};

	/// TRUE when frame is from `from` to `to`, both included; never when `to` is 0 (no window).
	Bool inWindow( UnsignedInt from, UnsignedInt to, UnsignedInt frame );

	/// TRUE when -crcLog writes a line at this frame. engineComputed: the engine computed the CRC at this frame itself.
	Bool crcLineDue( const CrcSchedule &schedule, UnsignedInt frame, Bool engineComputed );

	/// TRUE when the lines carry the seed column (-crcEvery or -crcWindow given).
	Bool hasSeedColumn( const CrcSchedule &schedule );

	/// The -crcLog line for one frame, with its newline; returns the length written (0 if the buffer is too small).
	Int formatCrcLine( char *buf, size_t size, const CrcSchedule &schedule, UnsignedInt frame, UnsignedInt crc, UnsignedInt seed );

	/// A positive frame count ("1" to "4000000000", digits only). Returns FALSE and leaves out alone otherwise.
	Bool parseCount( const char *text, UnsignedInt &out );

	/// A frame window "<from> <to>": digits only, from 1 or more, to at least from. Returns FALSE and leaves the outputs alone otherwise.
	Bool parseWindow( const char *fromText, const char *toText, UnsignedInt &from, UnsignedInt &to );

	/// The part of a source path after its last '/' or '\' (the whole path when it has none; "" for nullptr).
	const char *baseName( const char *path );

	/// One -aocRandomTrace line for an integer draw, with its newline; kind 'I' or 'u'. Returns the length (0 if too small).
	Int formatRandomInt( char *buf, size_t size, UnsignedInt frame, Bool inLogicUpdate, char kind, const char *file, Int line, Int lo, Int hi, Int value );

	/// One -aocRandomTrace line for a real draw, with its newline; kind 'R' or 'r'. Values with 9 significant digits,
	/// which round-trips a 32-bit float. Returns the length (0 if too small).
	Int formatRandomReal( char *buf, size_t size, UnsignedInt frame, Bool inLogicUpdate, char kind, const char *file, Int line, Real lo, Real hi, Real value );

	/// The -aocRandomTrace marker of a frame's CRC point, with its newline. Returns the length (0 if too small).
	Int formatCrcPoint( char *buf, size_t size, UnsignedInt frame, UnsignedInt seed );
}
