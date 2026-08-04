// bant - Bazel Navigation Tool
// Copyright (C) 2026 Henner Zeller <h.zeller@acm.org>
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

#include "bant/explore/source-finder.h"

#include <string_view>
#include <vector>

#include "bant/util/file-utils.h"
#include "bant/util/filesystem.h"

// clang-format off
static constexpr std::string_view kGeneratedLocations[] = {
  "bazel-bin/",
  "bazel-out/host/bin/",
  "bazel-genfiles/",  // Before bazel 1.1
};
// clang-format on

namespace bant {

// Depending on bazel version, there might be a different set of directories
// available that can contain generated files.
static std::vector<std::string_view> AvailableLocations() {
  std::vector<std::string_view> existing;
  existing.emplace_back("");  // We always look at toplevel.
  Filesystem &fs = Filesystem::instance();
  for (const std::string_view search_path : kGeneratedLocations) {
    if (!fs.ReadDir(search_path).empty()) {
      existing.emplace_back(search_path);
    }
  }
  return existing;
}

std::vector<PhysicalSourcePath> PossibleSourceLocations(std::string_view src) {
  static const auto sValidLocations = AvailableLocations();
  std::vector<PhysicalSourcePath> result;
  bool is_generated = false;
  for (const std::string_view search_path : sValidLocations) {
    result.emplace_back(FilesystemPath(search_path, src), is_generated);
    is_generated = true;  // Only the first in list is direct source
  }
  return result;
}

bool LooksLikeGeneratedProjectSource(std::string_view path) {
  for (const std::string_view possible_src_prefix : kGeneratedLocations) {
    if (path.starts_with(possible_src_prefix)) return true;
  }
  return false;
}
}  // namespace bant
