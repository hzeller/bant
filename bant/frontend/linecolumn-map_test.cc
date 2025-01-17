// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>
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

#include "bant/frontend/linecolumn-map.h"

#include <cstddef>
#include <sstream>
#include <string_view>

#include "absl/log/check.h"
#include "bant/frontend/source-locator.h"
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

// Find given string in "haystack" and return substring from that haystack.
static std::string_view FindReturnSubstr(std::string_view needle,
                                         std::string_view haystack) {
  const size_t found = haystack.find(needle);
  CHECK_NE(found, std::string_view::npos);
  return std::string_view{haystack.begin() + found, needle.length()};
}

TEST(LineColumnTextTest, InitializeFromRange) {
  constexpr std::string_view kText = R"(
line 2
line 3
  line 4)";  // No line ending here.
  LineColumnMap line_col_map;
  line_col_map.InitializeFromStringView(kText);
  EXPECT_EQ(line_col_map.GetRange(FindReturnSubstr("line 2", kText)),
            LineColumnRange({.start = {1, 0}, .end = {1, 6}}));
  EXPECT_EQ(line_col_map.GetRange(FindReturnSubstr("line 4", kText)),
            LineColumnRange({.start = {3, 2}, .end = {3, 8}}));
}
}  // namespace bant
