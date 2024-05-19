// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
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

#include "bant/frontend/linecolumn-map.h"

#include <algorithm>
#include <cstddef>
#include <string_view>

#include "absl/log/check.h"
#include "bant/frontend/source-locator.h"

namespace bant {
void LineColumnMap::PushNewline(std::string_view::const_iterator newline_pos) {
  CHECK(line_map_.empty() || line_map_.back() <= newline_pos);
  line_map_.push_back(newline_pos);
}

LineColumn LineColumnMap::GetPos(std::string_view::const_iterator pos) const {
  auto start = std::upper_bound(line_map_.begin(), line_map_.end(), pos);
  CHECK(start - line_map_.begin() > 0);
  --start;
  LineColumn result;
  result.line = start - line_map_.begin();
  result.col = pos - *start;
  return result;
}

LineColumnRange LineColumnMap::GetRange(std::string_view text) const {
  LineColumnRange result;
  result.start = GetPos(text.begin());
  result.end = GetPos(text.end());
  return result;
}

void LineColumnMap::InitializeFromStringView(std::string_view str) {
  CHECK(empty());  // Can only initialize once.
  PushNewline(str.begin());
  const size_t end_of_string = str.end() - str.begin();
  for (size_t pos = 0; pos < end_of_string; /**/) {
    pos = str.find_first_of('\n', pos);
    if (pos == std::string_view::npos) break;
    pos = pos + 1;
    PushNewline(str.begin() + pos);
  }
}
}  // namespace bant
