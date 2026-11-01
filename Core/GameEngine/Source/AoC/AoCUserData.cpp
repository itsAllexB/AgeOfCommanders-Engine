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

// AoCUserData.cpp
// Age of Commanders F12a: the per-run user-data folder of -aocUserData, the pure part. See AoCUserData.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "AoC/AoCUserData.h"

#include <string.h>
#include <vector>

namespace
{

Bool isSeparator( char c, Bool windowsRules )
{
	return c == '/' || (windowsRules && c == '\\');
}

Bool isDriveLetter( char c )
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

std::string trimmed( const char *text )
{
	if (text == nullptr)
		return std::string();
	const char *begin = text;
	while (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n')
		++begin;
	const char *end = begin + strlen(begin);
	while (end > begin && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
		--end;
	return std::string(begin, end);
}

// Characters Windows never allows in a path (a ':' only as the drive's). A stray '"' is the usual sign of a quoted
// argument ending in a backslash, which escaped the closing quote.
Bool hasInvalidWindowsCharacter( const std::string &p )
{
	for (size_t i = 0; i < p.size(); ++i)
	{
		const char c = p[i];
		if ((unsigned char)c < 32 || c == '"' || c == '<' || c == '>' || c == '|' || c == '?' || c == '*')
			return TRUE;
		if (c == ':' && i != 1)
			return TRUE;
	}
	return FALSE;
}

// Appends the segments of p from pos on, resolving "." (dropped), ".." (drops the segment before it, if any) and
// repeated separators.
void appendSegments( const std::string &p, size_t pos, Bool windowsRules, std::vector<std::string> &segments )
{
	while (pos < p.size())
	{
		while (pos < p.size() && isSeparator(p[pos], windowsRules))
			++pos;
		size_t end = pos;
		while (end < p.size() && !isSeparator(p[end], windowsRules))
			++end;
		if (end > pos)
		{
			const std::string segment = p.substr(pos, end - pos);
			if (segment == "..")
			{
				if (!segments.empty())
					segments.pop_back();
			}
			else if (segment != ".")
			{
				segments.push_back(segment);
			}
		}
		pos = end;
	}
}

// Splits an absolute path into its root ("C:", "\\server\share", or "" for POSIX's '/') and its resolved segments.
// FALSE when p is not absolute under the rules.
Bool splitAbsolute( const std::string &p, Bool windowsRules, std::string &root, std::vector<std::string> &segments )
{
	size_t pos = 0;
	if (windowsRules)
	{
		if (p.size() >= 2 && isSeparator(p[0], TRUE) && isSeparator(p[1], TRUE))
		{
			// UNC: \\server\share, both parts required
			const size_t serverBegin = 2;
			size_t serverEnd = serverBegin;
			while (serverEnd < p.size() && !isSeparator(p[serverEnd], TRUE))
				++serverEnd;
			if (serverEnd == serverBegin || serverEnd >= p.size())
				return FALSE;
			const size_t shareBegin = serverEnd + 1;
			size_t shareEnd = shareBegin;
			while (shareEnd < p.size() && !isSeparator(p[shareEnd], TRUE))
				++shareEnd;
			if (shareEnd == shareBegin)
				return FALSE;
			root = "\\\\" + p.substr(serverBegin, serverEnd - serverBegin) + "\\" + p.substr(shareBegin, shareEnd - shareBegin);
			pos = shareEnd;
		}
		else if (p.size() >= 3 && isDriveLetter(p[0]) && p[1] == ':' && isSeparator(p[2], TRUE))
		{
			root = p.substr(0, 2);
			pos = 2;
		}
		else
		{
			return FALSE;
		}
	}
	else
	{
		if (p.empty() || p[0] != '/')
			return FALSE;
		root.clear();
	}
	segments.clear();
	appendSegments(p, pos, windowsRules, segments);
	return TRUE;
}

} // namespace

Bool AoCUserData::resolveFor( const char *arg, const char *baseDir, Bool windowsRules, std::string &out )
{
	out.clear();
	const std::string p = trimmed(arg);
	if (p.empty())
		return FALSE;
	if (windowsRules && hasInvalidWindowsCharacter(p))
		return FALSE;

	std::string root;
	std::vector<std::string> segments;
	if (!splitAbsolute(p, windowsRules, root, segments))
	{
		if (windowsRules && p.size() >= 2 && isDriveLetter(p[0]) && p[1] == ':')
			return FALSE;	// drive-relative ("C:x")
		if (windowsRules && p.size() >= 2 && isSeparator(p[0], TRUE) && isSeparator(p[1], TRUE))
			return FALSE;	// a UNC path without its share
		const std::string base = trimmed(baseDir);
		if (windowsRules && hasInvalidWindowsCharacter(base))
			return FALSE;
		if (!splitAbsolute(base, windowsRules, root, segments))
			return FALSE;
		if (isSeparator(p[0], windowsRules))
			segments.clear();	// rooted without a drive: the root of the base's drive or share
		appendSegments(p, 0, windowsRules, segments);
	}

	const char separator = windowsRules ? '\\' : '/';
	out = root;
	out += separator;
	for (size_t i = 0; i < segments.size(); ++i)
	{
		out += segments[i];
		out += separator;
	}
	return TRUE;
}

std::string AoCUserData::instanceName( const char *firstInstanceName, const char *folder )
{
	unsigned long long hash = 14695981039346656037ULL;	// FNV-1a, 64 bits
	for (const char *p = folder ? folder : ""; *p != '\0'; ++p)
	{
		char c = *p;
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		hash ^= (unsigned char)c;
		hash *= 1099511628211ULL;
	}
	char digits[17];
	for (int i = 15; i >= 0; --i)
	{
		digits[i] = "0123456789ABCDEF"[hash & 0xF];
		hash >>= 4;
	}
	digits[16] = '\0';
	return std::string(firstInstanceName ? firstInstanceName : "") + "-AoCUserData-" + digits;
}

Bool AoCUserData::resolve( const char *arg, const char *baseDir, std::string &out )
{
#ifdef _WIN32
	return resolveFor(arg, baseDir, TRUE, out);
#else
	return resolveFor(arg, baseDir, FALSE, out);
#endif
}
