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

#include "bant/tool/dwyu.h"

#include <cstddef>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "bant/frontend/linecolumn-map.h"
#include "bant/frontend/named-content.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::ElementsAre;

namespace bant {

static LineColumn PosOfPart(const NamedLineIndexedContent &src,
                            const std::vector<std::string_view> &parts,
                            size_t i) {
  CHECK(i <= parts.size());
  return src.GetRange(parts[i]).start;
}

// Inception deception:
// Well, the following with a string in a string will create a warning if
// running bant on bant becaue the include in string is seen as toplevel inc.
// So, to avoid that, the include is actually an legitimate bant include which
// makes bant happy (until we start warning that the same header is included
// twice).
TEST(DWYUTest, HeaderFilesAreExtracted) {
  constexpr std::string_view kTestContent = R"(  // line 0
/* some ignored text in line 1 */
#include "CaSe-dash_underscore.h"
#include <should_not_be_extracted>
// #include "also-not-extracted.h"
   #include "but-this.h"
#include "with/suffix.hh"      // other ..
#include "with/suffix.pb.h"
#include "with/suffix.inc"     // .. common suffices
R"(
#include "bant/tool/dwyu.h"   // include embedded in string ignored.
")
#include    "w/space.h"        // even strange spacing should work
#include /* foo */ "this-is-silly.h"  // Some things are too far :)
)";
  NamedLineIndexedContent scanned_src("<text>", kTestContent);
  const auto includes = ExtractCCIncludes(&scanned_src);
  EXPECT_THAT(includes, ElementsAre("CaSe-dash_underscore.h", "but-this.h",
                                    "with/suffix.hh", "with/suffix.pb.h",
                                    "with/suffix.inc", "w/space.h"));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 0), (LineColumn{2, 10}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 1), (LineColumn{5, 13}));

  EXPECT_EQ(PosOfPart(scanned_src, includes, 2), (LineColumn{6, 10}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 3), (LineColumn{7, 10}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 4), (LineColumn{8, 10}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 5), (LineColumn{12, 13}));
}
}  // namespace bant
