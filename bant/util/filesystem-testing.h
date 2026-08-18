// bant - Bazel Navigation Tool
// Copyright (C) 2025 Henner Zeller <h.zeller@acm.org>
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

#ifndef BANT_UTIL_FILESYSTEM_TESTING_
#define BANT_UTIL_FILESYSTEM_TESTING_

#include <string_view>
#include <utility>
#include <vector>

#include "bant/util/filesystem.h"

namespace bant::test {
class FilesystemTesting {
 public:
  static void EvictCache(Filesystem &fs) { fs.EvictCache(); }

  // Insert file contents to be returned under the given paths; also populates
  // the corresponding directories.
  static void InjectTestFileContents(
    Filesystem &fs,
    const std::vector<std::pair<std::string_view, std::string_view>> &content) {
    fs.InjectTestFileContents(content);
  }
};
}  // namespace bant::test
#endif  // BANT_UTIL_FILESYSTEM_TESTING_
