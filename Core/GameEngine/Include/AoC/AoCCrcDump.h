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

// AoCCrcDump.h
// Age of Commanders F14a: the -crcDump harness flag (design D11 F12 and F14). A text decomposition of the per-frame logic
// CRC (GameLogic::getCRC), so two runs that part can be diffed down to the object and the field.
//
// Per dumped frame, at the point of GameLogic::update where the frame's CRC is taken (the harness frame hook):
//   frame <N> crc <the frame's CRC or -> recomputed <the same sequence recomputed here> seed <GetGameLogicRandomSeedCRC>
//   sections objects <crc> partition <crc> players <crc> ai <crc>         each section on its own, in getCRC's order
//   obj <id> <template> crc <Object::crc> mtx <transform> health <bits> wpn <weapons> exp <experience> upg <upgrades>
//       bonus <weapon bonus bits> scalar <damage scalar bits> mods <module crcs> pos <x>,<y>,<z>
// and, for a full dump (the single frame, or the last frame of a range), also, each with the bytes its crc function fed
// the CRC in hex last (so a diff names the field: tools/engine/crcdump_diff.py prints the first differing byte):
//   mod <object id> <index> <module class> <module tag> <crc> <bytes>   each behaviour module whose crc writes anything
//   player <index> <crc> <bytes>
//   cellrow <y> <crc>                                             the partition cells of one row
//   cell <x> <y> <bytes>                                          one partition cell (16 shroud levels, then x and y)
//   ai pathfinder <crc> aidata <crc> groups <count> lastgroupid <n>
//   pathfinder <bytes>
//   group <id> size <n> crc <crc> <bytes>
//   cost modulecrc <ms> objectsection <ms> objects <n> modules <n> the wall time of the module crc pass a mod game adds
//                                                                 to Object::crc, against the whole objects section
// Every value comes from the stock crc functions (XferCRC, read only) and public getters: the dump never runs a save-mode
// xfer and never changes logic state, so a run with the flag keeps the CRC log of the run without it.

#pragma once

#include "Lib/BaseType.h"

#include <stdio.h>

namespace AoCCrcDump
{
	/// Write one frame's dump to 'fp'. 'frameHasCRC' and 'frameCRC' are what GameLogic::update just computed (the CRC
	/// line is checked against the recomputed value only when the frame has one).
	void write( FILE *fp, UnsignedInt frame, Bool frameHasCRC, UnsignedInt frameCRC, Bool full );
}
