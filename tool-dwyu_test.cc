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

#include "tool-dwyu.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::ElementsAre;

namespace bant {
// TODO: need some bant-nolint to not stumble upon the following includes :)
TEST(DWYUTest, HeaderFilesAreExtracted) {
  constexpr std::string_view kTestContent = R"(
/* some ignored text */
#include "CaSe-dash_underscore.h"
#include <should_not_be_extracted>
// #include "also-not-extracted.h"
   #include "but-this.h"
#include "with/suffix.hh"      // other ..
#include "with/suffix.inc"     // .. common suffices
#include    "w/space.h"        // even strange spacing should work
#include /* foo */ "this-is-silly.h"  // Some things are too far :)
)";
  std::vector<std::string> headers = ExtractCCIncludes(kTestContent);
  EXPECT_THAT(headers,
              ElementsAre("CaSe-dash_underscore.h", "but-this.h",
                          "with/suffix.hh", "with/suffix.inc", "w/space.h"));
}
}  // namespace bant
