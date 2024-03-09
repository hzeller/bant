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

#include "absl/log/check.h"
#include "bant/frontend/linecolumn-map.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::ElementsAre;

namespace bant {

static LineColumn PosOfPart(const ContentPartsExtraction &extract, size_t i) {
  CHECK(i <= extract.parts.size());
  return extract.location_mapper.GetPos(extract.parts[i].begin());
}

// TODO: need some bant-nolint to not stumble upon the following includes :)
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
#include    "w/space.h"        // even strange spacing should work
#include /* foo */ "this-is-silly.h"  // Some things are too far :)
)";
  const ContentPartsExtraction extraction = ExtractCCIncludes(kTestContent);
  EXPECT_THAT(
    extraction.parts,
    ElementsAre("CaSe-dash_underscore.h", "but-this.h", "with/suffix.hh",
                "with/suffix.pb.h", "with/suffix.inc", "w/space.h"));
  EXPECT_EQ(PosOfPart(extraction, 0), (LineColumn{2, 10}));
  EXPECT_EQ(PosOfPart(extraction, 1), (LineColumn{5, 13}));

  EXPECT_EQ(PosOfPart(extraction, 2), (LineColumn{6, 10}));
  EXPECT_EQ(PosOfPart(extraction, 3), (LineColumn{7, 10}));
  EXPECT_EQ(PosOfPart(extraction, 4), (LineColumn{8, 10}));
  EXPECT_EQ(PosOfPart(extraction, 5), (LineColumn{9, 13}));
}
}  // namespace bant
