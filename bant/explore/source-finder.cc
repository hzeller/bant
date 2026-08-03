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

#include <optional>
#include <string_view>

#include "bant/util/file-utils.h"
#include "bant/util/filesystem.h"

#define LINK_PREFIX "bazel-"
// clang-format off
static constexpr std::string_view kSourceLocations[] = {
  "",
  (LINK_PREFIX "bin/"),
  (LINK_PREFIX "out/host/bin/"),
  (LINK_PREFIX "genfiles/"),  // Before bazel 1.1
};
// clang-format on
#undef LINK_PREFIX

namespace bant {
std::optional<PhysicalSourcePath> PathForProjectSource(std::string_view src) {
  Filesystem &fs = Filesystem::instance();
  PhysicalSourcePath result;
  result.is_generated = false;
  for (const std::string_view search_path : kSourceLocations) {
    result.path = FilesystemPath(search_path, src);
    if (fs.Exists(result.path.path())) {
      return result;
    }
    result.is_generated = true;  // Only the first in list is direct source
  }
  return std::nullopt;
}

bool LooksLikeGeneratedProjectSource(std::string_view path) {
  for (const std::string_view possible_src_prefix : kSourceLocations) {
    if (possible_src_prefix.empty()) continue;  // First one is _not_ generated
    if (path.starts_with(possible_src_prefix)) return true;
  }
  return false;
}
}  // namespace bant
