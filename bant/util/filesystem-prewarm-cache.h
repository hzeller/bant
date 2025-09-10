// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#ifndef BANT_FILESYSTEM_PREWARM_CACHE_H
#define BANT_FILESYSTEM_PREWARM_CACHE_H

#include <string_view>

#include "bant/session.h"

namespace bant {
// Best-effort helper to prewarm the OS filesystem cache.
//
// If the bant-processed source code is on some network file system, accessing
// files the first time might be slow. This can speed up if we can access
// all the files before they are used, 'prewarming' the OS filesystem cache.
//
// These functions help to keep track of filesystem accesses and prewarming
// the filesystem next time we start up. No direct interaction with the
// functionality of bant, just indirectly by replaying last filesystem accesses
// on start-up with multiple threads, preparing the linear accesses by bant
// to be potentially faster. Slow physical media (network, HDD) will benefit; no
// measurable impact with SSDs.

// Initialize cache. Iff there is a directory ~/.cache/bant, it will store
// and retrieve files there. If not, caching is disabled.
// Filenames are based on arguments and the project directory.
// If init finds an existing file, it attempts to warm the OS filesystem cache.
void FilesystemPrewarmCacheInit(bant::Session &session, int argc, char *argv[]);

// Tell prewarm cache of future invocations that we just accessed a file.
bool FilesystemPrewarmCacheRememberFileWasAccessed(std::string_view file);

// Tell prewarm cache of future invocations that we just accessed a directory.
bool FilesystemPrewarmCacheRememberDirWasAccessed(std::string_view dir);

}  // namespace bant

#endif  // BANT_FILESYSTEM_PREWARM_CACHE_H
