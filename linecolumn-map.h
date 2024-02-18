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

#ifndef BANT_LINE_COLUMN_MAP_
#define BANT_LINE_COLUMN_MAP_

#include <ostream>
#include <vector>

// A utility to map positions in a string_view to a human-consumable
// line/column representation.

namespace bant {
// Zero-based line and column.
struct LineColumn {
  int line;
  int col;
};

// Print line and column; one-based for easier human consumption.
std::ostream &operator<<(std::ostream &out, LineColumn);

struct LineColumnRange {
  LineColumn start;  // inclusive
  LineColumn end;    // exclusive
};

std::ostream &operator<<(std::ostream &out, const LineColumnRange &);

// A line column map has to be fed with positions of newlines. It can answer
// questions of a position of a particular std::string_view within the
// larger string view.
// This allows a lightweight way to provide human-readable Lines and Columns
// without the overhead to attach it to every Token. The Token's string_view
// in itself has all necessary information to recover that.
class LineColumnMap {
 public:
  // Push the position after the last newline. Typically done by the scanner.
  void PushNewline(std::string_view::const_iterator newline_pos);

  bool empty() const { return line_map_.empty(); }
  size_t lines() const { return line_map_.size(); }

  // Return position of given text that needs to be within content of
  // tokens already seen.
  LineColumn GetPos(std::string_view text) const;
  LineColumnRange GetRange(std::string_view text) const;

 private:
  // Contains position at the beginning of each line. Stricly ordered.
  std::vector<std::string_view::const_iterator> line_map_;
};
}  // namespace bant

#endif  // BANT_LINE_COLUMN_MAP_
