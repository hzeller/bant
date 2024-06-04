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

#include "bant/frontend/named-content.h"

#include <string_view>

#include "gtest/gtest.h"

namespace bant {
TEST(NamedContent, Surrounding) {
  {
    constexpr std::string_view content = "foo";
    const NamedLineIndexedContent nc("file.txt", content);
    const auto full_line = nc.GetSurroundingLine(content.substr(1, 1));
    EXPECT_EQ(full_line, content);
  }
  {
    constexpr std::string_view content = "\nfoo\n";
    const NamedLineIndexedContent nc("file.txt", content);
    const auto full_line = nc.GetSurroundingLine(content.substr(1, 1));
    EXPECT_EQ(full_line, content.substr(1, 3));
  }
  {
    constexpr std::string_view content = "foo\nbar\nbaz";
    const NamedLineIndexedContent nc("file.txt", content);
    const auto full_line = nc.GetSurroundingLine(content.substr(5, 1));
    EXPECT_EQ(full_line, content.substr(4, 3));
  }
}
}  // namespace bant
