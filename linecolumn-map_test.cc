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

#include "linecolumn-map.h"

#include <sstream>

#include "gtest/gtest.h"

namespace bant {

struct LineColumnTestData {
  LineColumn line_col;
  const char *expected;
};

// Test test verifies that line-column offset appear to the user correctly.
TEST(LineColumnTextTest, PrintLineColumn) {
  static constexpr LineColumnTestData text_test_data[] = {
    {{0, 0}, "1:1"},
    {{0, 1}, "1:2"},
    {{1, 0}, "2:1"},
    {{10, 8}, "11:9"},
  };
  for (const auto &test_case : text_test_data) {
    std::ostringstream oss;
    oss << test_case.line_col;
    EXPECT_EQ(oss.str(), test_case.expected);
  }
}

struct LineColumnRangeTestData {
  LineColumnRange range;
  const char *expected;
};

TEST(LineColumnTextTest, PrintLineColumnRange) {
  static constexpr LineColumnRangeTestData text_test_data[] = {
    {{{0, 0}, {0, 7}}, "1:1-7:"},  // Same line, multiple columns
    {{{0, 1}, {0, 3}}, "1:2-3:"},
    {{{1, 0}, {2, 14}}, "2:1:3:14:"},  // start/end different lines
    {{{10, 8}, {11, 2}}, "11:9:12:2:"},
    {{{10, 8}, {10, 9}}, "11:9:"},  // Single character range
    {{{10, 8}, {10, 8}}, "11:9:"},  // Empty range.
  };
  for (const auto &test_case : text_test_data) {
    std::ostringstream oss;
    oss << test_case.range;
    EXPECT_EQ(oss.str(), test_case.expected);
  }
}
}  // namespace bant
