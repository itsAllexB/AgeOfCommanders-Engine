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

// AoC F12a: the pure part of -aocUserData (design D11 section 2.12): how the flag's argument becomes the user-data folder
// (absolute paths, relative ones against the base, ".." and "." and repeated separators, the Windows and the POSIX rules,
// the refused shapes), the instance mutex name per folder, and that the folders the engine derives from it (replays,
// user maps) follow an installed one.

#include <stdio.h>
#include <string.h>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <gtest/gtest.h>

#include "AoC/AoCUserData.h"
#include "Common/GlobalData.h"
#include "Common/Recorder.h"
#include "GameClient/MapUtil.h"

namespace
{

std::string win( const char *arg, const char *base = "C:\\work\\build" )
{
	std::string out;
	return AoCUserData::resolveFor(arg, base, TRUE, out) ? out : std::string("<refused>");
}

std::string posix( const char *arg, const char *base = "/work/build" )
{
	std::string out;
	return AoCUserData::resolveFor(arg, base, FALSE, out) ? out : std::string("<refused>");
}

} // namespace

TEST(AoCUserDataTest, AbsoluteWindowsPathsKeepTheirRootAndGetOneTrailingSeparator)
{
	EXPECT_EQ(win("C:\\Users\\x\\run1"), "C:\\Users\\x\\run1\\");
	EXPECT_EQ(win("C:\\Users\\x\\run1\\"), "C:\\Users\\x\\run1\\");
	EXPECT_EQ(win("C:/Users//x/run1//"), "C:\\Users\\x\\run1\\");
	EXPECT_EQ(win("d:\\a b\\c.d"), "d:\\a b\\c.d\\");
	EXPECT_EQ(win("C:\\"), "C:\\");
	EXPECT_EQ(win("  C:\\a\\b  "), "C:\\a\\b\\");	// surrounding blanks are not part of the name
	EXPECT_EQ(win("C:\\a\\b", ""), "C:\\a\\b\\");	// an absolute path needs no base
}

TEST(AoCUserDataTest, RelativeWindowsPathsJoinTheBaseAndResolveDots)
{
	EXPECT_EQ(win("run1"), "C:\\work\\build\\run1\\");
	EXPECT_EQ(win("f12a\\userdata\\gate-a"), "C:\\work\\build\\f12a\\userdata\\gate-a\\");
	EXPECT_EQ(win("f12a/userdata/gate-a/"), "C:\\work\\build\\f12a\\userdata\\gate-a\\");
	EXPECT_EQ(win(".\\a\\.\\b"), "C:\\work\\build\\a\\b\\");
	EXPECT_EQ(win("..\\x"), "C:\\work\\x\\");
	EXPECT_EQ(win("a\\..\\..\\x"), "C:\\work\\x\\");
	EXPECT_EQ(win("..\\..\\..\\..\\x"), "C:\\x\\");	// ".." at the root stays at the root
	EXPECT_EQ(win("C:\\a\\b\\..\\c\\."), "C:\\a\\c\\");
	EXPECT_EQ(win("x", "C:\\work\\build\\"), "C:\\work\\build\\x\\");
	EXPECT_EQ(win("C:\\data\\.", ""), "C:\\data\\");	// the replay workers pass "<dir>." (ReplaySimulation)
}

TEST(AoCUserDataTest, RootedWithoutADriveTakesTheBaseDriveOrShare)
{
	EXPECT_EQ(win("\\tmp\\x", "D:\\work"), "D:\\tmp\\x\\");
	EXPECT_EQ(win("/tmp/x", "D:\\work"), "D:\\tmp\\x\\");
	EXPECT_EQ(win("\\x", "\\\\server\\share\\work"), "\\\\server\\share\\x\\");
}

TEST(AoCUserDataTest, UncPathsNeedServerAndShare)
{
	EXPECT_EQ(win("\\\\server\\share\\runs\\a"), "\\\\server\\share\\runs\\a\\");
	EXPECT_EQ(win("//server/share"), "\\\\server\\share\\");
	EXPECT_EQ(win("\\\\server\\share\\..\\.."), "\\\\server\\share\\");	// ".." never climbs out of the share
	EXPECT_EQ(win("run", "\\\\server\\share\\work"), "\\\\server\\share\\work\\run\\");
	EXPECT_EQ(win("\\\\server"), "<refused>");
	EXPECT_EQ(win("\\\\server\\"), "<refused>");
}

TEST(AoCUserDataTest, WindowsRefusesWhatItCannotNameSafely)
{
	EXPECT_EQ(win(""), "<refused>");
	EXPECT_EQ(win("   "), "<refused>");
	std::string out = "unchanged";
	EXPECT_FALSE(AoCUserData::resolveFor(nullptr, "C:\\w", TRUE, out));
	EXPECT_TRUE(out.empty());
	EXPECT_EQ(win("C:run"), "<refused>");				// drive-relative: depends on a per-drive current folder
	EXPECT_EQ(win("C:\\run1\""), "<refused>");		// "C:\run1\" quoted: the backslash escaped the closing quote
	EXPECT_EQ(win("a|b"), "<refused>");
	EXPECT_EQ(win("a*"), "<refused>");
	EXPECT_EQ(win("C:\\a:b"), "<refused>");
	EXPECT_EQ(win("run", ""), "<refused>");			// relative without a base
	EXPECT_EQ(win("run", "work\\build"), "<refused>");	// relative against a relative base
	EXPECT_EQ(win("run", nullptr), "<refused>");
}

TEST(AoCUserDataTest, PosixRules)
{
	EXPECT_EQ(posix("/home/u/run1"), "/home/u/run1/");
	EXPECT_EQ(posix("/home//u/./run1/"), "/home/u/run1/");
	EXPECT_EQ(posix("run1"), "/work/build/run1/");
	EXPECT_EQ(posix("../x"), "/work/x/");
	EXPECT_EQ(posix("../../../x"), "/x/");
	EXPECT_EQ(posix("a\\b"), "/work/build/a\\b/");	// a backslash is an ordinary character
	EXPECT_EQ(posix("C:\\x"), "/work/build/C:\\x/");	// and so is a colon
	EXPECT_EQ(posix("/"), "/");
	EXPECT_EQ(posix("x", ""), "<refused>");
	EXPECT_EQ(posix("x", "relative/base"), "<refused>");
	EXPECT_EQ(posix(""), "<refused>");
}

TEST(AoCUserDataTest, ResolveUsesTheRulesOfThePlatform)
{
	std::string a, b;
#ifdef _WIN32
	EXPECT_TRUE(AoCUserData::resolve("x\\y", "C:\\w", a));
	EXPECT_TRUE(AoCUserData::resolveFor("x\\y", "C:\\w", TRUE, b));
#else
	EXPECT_TRUE(AoCUserData::resolve("x/y", "/w", a));
	EXPECT_TRUE(AoCUserData::resolveFor("x/y", "/w", FALSE, b));
#endif
	EXPECT_EQ(a, b);
}

TEST(AoCUserDataTest, OneInstanceNamePerFolder)
{
	const char *guid = "685EAFF2-3216-4265-B047-251C5F4B82F3";
	const std::string a = AoCUserData::instanceName(guid, "C:\\work\\build\\f3\\userdata\\A1\\");
	EXPECT_EQ(a.compare(0, strlen(guid), guid), 0);	// the stock name first
	EXPECT_EQ(a.size(), strlen(guid) + strlen("-AoCUserData-") + 16);
	EXPECT_EQ(a.find('\\'), std::string::npos);			// a backslash would name a kernel namespace
	EXPECT_EQ(a, AoCUserData::instanceName(guid, "c:\\WORK\\build\\F3\\userdata\\a1\\"));	// Windows paths ignore case
	EXPECT_NE(a, AoCUserData::instanceName(guid, "C:\\work\\build\\f3\\userdata\\A2\\"));
	EXPECT_NE(a, AoCUserData::instanceName(guid, "C:\\work\\build\\f3\\userdata\\A1\\x\\"));
	EXPECT_NE(a, std::string(guid));									// never the stock name: a normal game can still start
	// FNV-1a 64 of the empty string is CBF29CE484222325
	EXPECT_EQ(AoCUserData::instanceName("G", ""), "G-AoCUserData-CBF29CE484222325");
}

namespace
{

// A scratch folder under the system temp folder, removed again by the destructor.
struct ScratchFolder
{
	std::filesystem::path path;
	ScratchFolder()
	{
		std::error_code ec;
		path = std::filesystem::temp_directory_path(ec) / ("aoc-userdata-test-" + std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(path, ec);
	}
	~ScratchFolder()
	{
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}
	std::string str() const { return path.string(); }
};

} // namespace

// The refusals parseAocUserData turns into exit code 6, through AoCUserData::prepare, which it calls.
TEST(AoCUserDataTest, PrepareRefusesTheExitSixCases)
{
	ScratchFolder scratch;
	ASSERT_TRUE(std::filesystem::is_directory(scratch.path));
	std::string out = "unchanged";

	EXPECT_EQ(AoCUserData::prepare(nullptr, scratch.str().c_str(), out), AoCUserData::PREPARE_NO_ARGUMENT);	// "-aocUserData" last
	EXPECT_TRUE(out.empty());
	EXPECT_EQ(AoCUserData::prepare("", scratch.str().c_str(), out), AoCUserData::PREPARE_BAD_NAME);
	EXPECT_EQ(AoCUserData::prepare("run", "", out), AoCUserData::PREPARE_BAD_NAME);						// relative, no base
#ifdef _WIN32
	EXPECT_EQ(AoCUserData::prepare("C:run", scratch.str().c_str(), out), AoCUserData::PREPARE_BAD_NAME);
#endif

	// a path naming an existing file, and a folder that cannot be created (under that file)
	const std::filesystem::path file = scratch.path / "a-file.txt";
	FILE *f = fopen(file.string().c_str(), "wb");
	ASSERT_NE(f, nullptr);
	fputs("x", f);
	fclose(f);
	EXPECT_EQ(AoCUserData::prepare(file.string().c_str(), "", out), AoCUserData::PREPARE_NOT_A_FOLDER);
	EXPECT_EQ(AoCUserData::prepare((file / "sub").string().c_str(), "", out), AoCUserData::PREPARE_NOT_A_FOLDER);
	EXPECT_TRUE(out.empty());
	EXPECT_TRUE(std::filesystem::is_regular_file(file));		// the file is left alone
}

TEST(AoCUserDataTest, PrepareRefusesAFolderLongerThan180Characters)
{
	ScratchFolder scratch;
	std::string out;
	std::string base = scratch.str();
	// pad with one segment so the resolved folder, separator included, is exactly the limit, then one more
	std::string resolved;
	ASSERT_TRUE(AoCUserData::resolve("x", base.c_str(), resolved));
	ASSERT_LT(resolved.size(), (size_t)AoCUserData::MAX_FOLDER_LENGTH);
	const size_t pad = AoCUserData::MAX_FOLDER_LENGTH - resolved.size() + 1;	// "x" grows to fill the limit
	const std::string atLimit(pad, 'x');
	ASSERT_TRUE(AoCUserData::resolve(atLimit.c_str(), base.c_str(), resolved));
	EXPECT_EQ(resolved.size(), (size_t)AoCUserData::MAX_FOLDER_LENGTH);
	EXPECT_EQ(AoCUserData::prepare(atLimit.c_str(), base.c_str(), out), AoCUserData::PREPARE_OK);
	EXPECT_EQ(out, resolved);
	const std::string overLimit(pad + 1, 'x');
	EXPECT_EQ(AoCUserData::prepare(overLimit.c_str(), base.c_str(), out), AoCUserData::PREPARE_TOO_LONG);
	EXPECT_TRUE(out.empty());
	EXPECT_FALSE(std::filesystem::exists(scratch.path / overLimit));	// refused before anything is created
}

TEST(AoCUserDataTest, PrepareCreatesTheFolderAndLeavesNoProbeFile)
{
	ScratchFolder scratch;
	std::string out;
	EXPECT_EQ(AoCUserData::prepare("runs/gate-b1", scratch.str().c_str(), out), AoCUserData::PREPARE_OK);
	const std::filesystem::path made = scratch.path / "runs" / "gate-b1";
	EXPECT_TRUE(std::filesystem::is_directory(made));
	EXPECT_TRUE(std::filesystem::is_empty(made));						// the write probe was deleted
	std::string again;
	EXPECT_EQ(AoCUserData::prepare(out.c_str(), "", again), AoCUserData::PREPARE_OK);	// an existing folder: the same
	EXPECT_EQ(again, out);
	EXPECT_STRNE(AoCUserData::describe(AoCUserData::PREPARE_TOO_LONG), AoCUserData::describe(AoCUserData::PREPARE_NOT_WRITABLE));
}

TEST(AoCUserDataTest, DerivedFoldersFollowAnInstalledFolder)
{
	ASSERT_NE(TheWritableGlobalData, nullptr);
	const AsciiString before = TheGlobalData->getPath_UserData();
	std::string dir;
#ifdef _WIN32
	ASSERT_TRUE(AoCUserData::resolve("C:\\aoc-test\\userdata\\gate-b1", "", dir));
	const char *const sep = "\\";
#else
	ASSERT_TRUE(AoCUserData::resolve("/aoc-test/userdata/gate-b1", "", dir));
	const char *const sep = "/";
#endif
	TheWritableGlobalData->setPath_UserData(AsciiString(dir.c_str()));

	EXPECT_EQ(std::string(TheGlobalData->getPath_UserData().str()), dir);
	EXPECT_EQ(std::string(RecorderClass::getReplayDir().str()), dir + "Replays" + sep);
	EXPECT_EQ(std::string(RecorderClass::getReplayPathForRead(AsciiString("00000000.rep")).str()), dir + "Replays" + sep + "00000000.rep");
	MapCache cache;
	EXPECT_EQ(std::string(cache.getUserMapDir().str()), dir + "Maps");

	TheWritableGlobalData->setPath_UserData(before);
	EXPECT_EQ(std::string(TheGlobalData->getPath_UserData().str()), std::string(before.str()));
}
