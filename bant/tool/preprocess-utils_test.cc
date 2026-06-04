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

#include "bant/tool/preprocess-utils.h"

#include <cstddef>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/source-locator.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::ElementsAre;

namespace bant {
static LineColumn PosOfPart(const NamedLineIndexedContent &src,
                            const std::vector<std::string_view> &parts,
                            size_t i) {
  CHECK(i <= parts.size());
  return src.GetLocation(parts[i]).line_column_range.start;
}

// Inception deception:
// Well, the following with a string in a string will create a warning if
// running bant on bant becaue the include in string is seen as toplevel inc.
// So, to avoid that, the include is actually an legitimate bant include which
// makes bant happy (until we start warning that the same header is included
// twice).
TEST(PreprocessUtils, HeaderFilesAreExtracted) {
  // Note, clang-format is seriously confused about the next lines and
  // it will also not work with switching it off ?
  // So we give up and have the order in which clang-format likes it.
  constexpr std::string_view kTestContent = R"inctest(  // line 0
/* some ignored text in line 1 */
#include "CaSe-dash_underscore.h"
#include <a_bracket_include>
// #include "not-extracted.h"
   #include "but-this.h"
#include "with/suffix.hh"      // other ..
#include "with/suffix.pb.h"
#include "with/suffix.inc"     // .. common suffices
R"(
#include "bant/tool/dwyu.h"   // include embedded in string ignored.
)"
R"xyz(
#include "absl/log/check.h"   // include embedded in string ignored.
)xyz"
#include "../dotdot.h"         // mmh, who is doing this ?
#include "more-special-c++.h"  // other characters used.
#include /* foo */ "this-is-silly.h"  // Some things are too far :)
#  include    "w/space.h"        // even strange spacing should work
// #include "not-seen.h"
// Here is a single quote-char (") which should not mess up the next include
#include "should-be-seen.h"  // another one (")
#include "this-as-well.h"
)inctest";
  NamedLineIndexedContent scanned_src("<text>", kTestContent);
  const auto includes = ExtractCCIncludes(&scanned_src);
  EXPECT_THAT(
    includes,
    ElementsAre("\"CaSe-dash_underscore.h", "<a_bracket_include",
                "\"but-this.h", "\"with/suffix.hh", "\"with/suffix.pb.h",
                "\"with/suffix.inc", "\"../dotdot.h", "\"more-special-c++.h",
                "\"w/space.h", "\"should-be-seen.h", "\"this-as-well.h"));
  // Let's check some locations.
  EXPECT_EQ(PosOfPart(scanned_src, includes, 0), (LineColumn{2, 9}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 1), (LineColumn{3, 9}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 2), (LineColumn{5, 12}));

  EXPECT_EQ(PosOfPart(scanned_src, includes, 3), (LineColumn{6, 9}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 4), (LineColumn{7, 9}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 5), (LineColumn{8, 9}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 8), (LineColumn{18, 14}));
}

TEST(PreprocessUtils, ProtoImportsAreExtracted) {
  constexpr std::string_view kTestProto = R"pb(
    syntax = "proto3"
    ;

package foo;

// import "not/extracted.proto";
import public "path/to/dependency.proto";
import "other/dep.proto";

message Bar {
  string name = 1;
}
)pb";
  NamedLineIndexedContent scanned_src("<proto>", kTestProto);
  const auto imports = ExtractProtoImports(&scanned_src);
  EXPECT_THAT(imports,
              ElementsAre("path/to/dependency.proto", "other/dep.proto"));
}

}  // namespace bant
