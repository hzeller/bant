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

#ifndef BANT_SOURCE_LOCATOR_
#define BANT_SOURCE_LOCATOR_

#include <string_view>

namespace bant {
// Zero-based line and column.
struct LineColumn {
  int line;
  int col;

  bool operator==(const LineColumn &) const = default;
};

// Print line and column; one-based for easier human consumption.
std::ostream &operator<<(std::ostream &out, LineColumn);

struct LineColumnRange {
  LineColumn start;  // inclusive
  LineColumn end;    // exclusive

  bool operator==(const LineColumnRange &) const = default;
};

std::ostream &operator<<(std::ostream &out, const LineColumnRange &);

// A SourceLocator can return the line-column location inside some content it
// is responsible for. This can typically be inside a file, but can also be
// a fixed location from content that is generated by expression evaluation.
class SourceLocator {
 public:
  virtual ~SourceLocator() = default;

  // Name of this content, typically the filename.
  virtual std::string_view source_name() const = 0;

  // Given "text", that must be a substring handled by this locator, return
  // range.
  virtual LineColumnRange GetLocation(std::string_view text) const = 0;

  // Given the string_view, that must be a substring handled by this locator,
  // format the location of that string view to stream as file.txt:<line>:<col>:
  std::ostream &Loc(std::ostream &out, std::string_view s) const;

  // Same, but instead of writing to stream, returning a string.
  std::string Loc(std::string_view s) const;
};
}  // namespace bant
#endif  // BANT_SOURCE_LOCATOR_
