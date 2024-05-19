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

#include "bant/frontend/source-locator.h"

#include <ostream>
#include <sstream>
#include <string>
#include <string_view>

namespace bant {
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

std::ostream &operator<<(std::ostream &out, const FileLocation &floc) {
  out << floc.filename << ":" << floc.line_column_range;
  return out;
}

std::ostream &SourceLocator::Loc(std::ostream &out, std::string_view s) const {
  out << GetLocation(s);
  return out;
}

std::string SourceLocator::Loc(std::string_view s) const {
  std::stringstream out;
  out << GetLocation(s);
  return out.str();
}
}  // namespace bant
