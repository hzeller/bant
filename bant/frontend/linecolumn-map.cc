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
#include <cassert>

namespace bant {
void LineColumnMap::PushNewline(std::string_view::const_iterator newline_pos) {
  assert(line_map_.empty() || line_map_.back() <= newline_pos);
  line_map_.push_back(newline_pos);
}

std::ostream &operator<<(std::ostream &out, LineColumn line_column) {
  out << (line_column.line + 1) << ":" << (line_column.col + 1);
  return out;
}

std::ostream &operator<<(std::ostream &out, const LineColumnRange &r) {
  // Unlike 'technical' representation where we point the end pos one past
  // the relevant range, for human consumption we want to point to the last
  // character.
  LineColumn right = r.end;
  right.col--;
  out << r.start;
  // Only if we cover more than a single character, print range of columns.
  if (r.start.line == right.line) {
    if (right.col > r.start.col) out << '-' << right.col + 1;
  } else {
    out << ':' << right;
  }
  out << ':';
  return out;
}

LineColumn LineColumnMap::GetPos(std::string_view text) const {
  auto start =
    std::upper_bound(line_map_.begin(), line_map_.end(), text.begin());
  assert(start - line_map_.begin() > 0);
  --start;
  LineColumn result;
  result.line = start - line_map_.begin();
  result.col = text.begin() - *start;
  return result;
}

LineColumnRange LineColumnMap::GetRange(std::string_view text) const {
  LineColumnRange result;
  result.start = GetPos(text);
  result.end = GetPos(text.end());
  return result;
}

}  // namespace bant
