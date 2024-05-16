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

#include "bant/util/disjoint-range-map.h"

#include <array>
#include <cstddef>
#include <string_view>

#include "gtest/gtest.h"

namespace bant {
TEST(DisjointRangeMap, RangeLookups) {
  DisjointRangeMap<std::string_view, size_t> subrange_map;
  constexpr std::array<std::string_view, 3> values{
    "Hello world", "Another text", "Yet another substring"};

  for (size_t i = 0; i < values.size(); ++i) {
    subrange_map.Insert(values[i], i);
  }

  // The following EXPECT_EQ() are annotated with NOLINT, as clang-tidy
  // fails to see that found has been checked for the optional value to exist.
  // Full range lookup
  for (size_t i = 0; i < values.size(); ++i) {
    auto found = subrange_map.FindBySubrange(values[i]);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found.value(), i);  // NOLINT
  }

  // At beginning of range
  for (size_t i = 0; i < values.size(); ++i) {
    auto found = subrange_map.FindBySubrange(values[i].substr(0, 3));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found.value(), i);  // NOLINT
  }

  // Empty range at beginning
  for (size_t i = 0; i < values.size(); ++i) {
    auto found = subrange_map.FindBySubrange(values[i].substr(0, 0));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found.value(), i);  // NOLINT
  }

  // middle range lookup
  for (size_t i = 0; i < values.size(); ++i) {
    auto found = subrange_map.FindBySubrange(values[i].substr(3, 7));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found.value(), i);  // NOLINT
  }

  // empty middle range.
  for (size_t i = 0; i < values.size(); ++i) {
    auto found = subrange_map.FindBySubrange(values[i].substr(8, 0));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found.value(), i);  // NOLINT
  }

  // At end
  for (size_t i = 0; i < values.size(); ++i) {
    auto found = subrange_map.FindBySubrange(values[i].substr(5));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found.value(), i);  // NOLINT
  }

  EXPECT_FALSE(subrange_map.FindBySubrange("different string").has_value());
}

}  // namespace bant
