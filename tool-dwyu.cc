// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "tool-dwyu.h"

#include <string>
#include <string_view>
#include <vector>

#include "re2/re2.h"

namespace bant {
std::vector<std::string> ExtractCCHeaders(std::string_view content) {
  static const LazyRE2 kIncMatch{R"/(#include\s+"([0-9a-zA-Z_/-]+\.h)")/"};

  std::vector<std::string> result;
  std::string header_path;
  while (RE2::FindAndConsume(&content, *kIncMatch, &header_path)) {
    result.push_back(header_path);
  }
  return result;
}
}  // namespace bant
