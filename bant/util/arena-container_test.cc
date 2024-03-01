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

#include "bant/util/arena-container.h"

#include "gtest/gtest.h"

namespace bant {
TEST(ArenaDeque, SimpleOps) {
  Arena a(1024);
  ArenaDeque<int, 3, 96> container;  // deliberately funky min..max

  // Make sure that multiple crossings of block-boundaries work well.
  for (int i = 0; i < 300; ++i) {
    container.Append(i, &a);
    int count = 0;
    for (int value : container) {
      EXPECT_EQ(value, count);
      count++;
    }

    for (int j = 0; j < i; ++j) {
      EXPECT_EQ(container[j], j);
    }
  }
}
}  // namespace bant
