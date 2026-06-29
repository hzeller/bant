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
#include "bant/explore/query-utils.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/parsed-project_testutil.h"
#include "bant/frontend/source-locator.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Pair;

namespace bant {
// NOLINTNEXTLINE appears unused, but is used by gmock printing
static void PrintTo(const TaggedInclude &i, std::ostream *out) {
  *out << (i.is_ifdefed_out ? "EXCLUDED: " : " ")
       << (i.is_angle_bracket ? "<" : "") << i.include
       << (i.is_angle_bracket ? ">" : "") << " ("
       << i.active_preprocessing_condition << " " << i.else_location << ")";
}
namespace {
TEST(PreprocessUtils, PreprocessRangeIf_0_1) {
  constexpr std::string_view kTestContent = R"deftest(
#if 0   // A constant. unambiguously excluded, not showing up in result
#include "A_TEXT.h"
#else
#include "A_PRIME_TEXT.h"
#endif
#if 1
#include "B_TEXT.h"
#else  // The following is unambiguously not included
#include "B_PRIME_TEXT.h"
#endif
)deftest";

  {
    DefineMap defs;
    NamedLineIndexedContent scanned_src("<text>", kTestContent);
    auto ranges = ExtractCCIncludes(&scanned_src, defs);
    using TI = TaggedInclude;
    EXPECT_THAT(
      ranges, ElementsAre(TI{"A_PRIME_TEXT.h", false, false, "#if 0", "#else"},
                          TI{"B_TEXT.h", false, false, "#if 1"}));
  }
}

TEST(PreprocessUtils, PreprocessSkipCommentAndStrings) {
  constexpr std::string_view kTestContent = R"deftest(
/*
#include "commented.h"
*/
R"(
#include "in-raw-string.h"
)"
#include "only-one-visible.h"
hello = "#include <include-inside-string>"
)deftest";

  {
    DefineMap defs;
    NamedLineIndexedContent scanned_src("<text>", kTestContent);
    auto ranges = ExtractCCIncludes(&scanned_src, defs);
    using TI = TaggedInclude;
    EXPECT_THAT(ranges, ElementsAre(TI{"only-one-visible.h", false, false}));
  }
}

TEST(PreprocessUtils, PreprocessRangeUnambiguousValueMacro) {
  constexpr std::string_view kTestContent = R"deftest(
#if FOO  // unambiguously excluded as FOO=0, not showing up in result
#  include "A_TEXT.h"  // space after pound
#else
#  include "A_PRIME_TEXT.h"
#endif
#if BAR  // not defined macro, so ambiguous. Emit, but mark as not included
#  include "B_TEXT.h"
#else
#include "B_PRIME_TEXT.h"
#endif
#if BAZ  // unambiguously included
  #include "C_TEXT.h"  // space before pount
#else    // the following is unambiguously _not_ included.
  #include "C_PRIME_TEXT.h"
#endif
)deftest";

  {
    DefineMap defs;
    defs["FOO"] = false;
    // BAR not defined
    defs["BAZ"] = true;
    NamedLineIndexedContent scanned_src("<text>", kTestContent);
    auto ranges = ExtractCCIncludes(&scanned_src, defs);
    using TI = TaggedInclude;
    EXPECT_THAT(
      ranges,
      ElementsAre(TI{"A_PRIME_TEXT.h", false, false, "#if FOO", "#else"},  //
                  TI{"B_TEXT.h", false, true, "#if BAR"},                  //
                  TI{"B_PRIME_TEXT.h", false, false, "#if BAR", "#else"},  //
                  TI{"C_TEXT.h", false, false, "#if BAZ"}                  //
                  ));
  }
}

TEST(PreprocessUtils, PreprocessRangeUnambiguousExistenceMacro) {
  constexpr std::string_view kTestContent = R"deftest(
#ifdef FOO
#include "A_TEXT.h"
#else   // unambiguously not included
#inlcude "A_PRIME_TEXT.h"
#endif
#ifndef BAR  // not defined macro, so ambiguous.
#include "B_TEXT.h"
#else        // Ambiguous text included but with 'false'
#include "B_PRIME_TEXT.h"
#endif
)deftest";

  {
    DefineMap defs;
    defs["FOO"] = false;  // We only check for ifdef, so value doesn't matter
    // BAR not defined
    NamedLineIndexedContent scanned_src("<text>", kTestContent);
    auto ranges = ExtractCCIncludes(&scanned_src, defs);
    using TI = TaggedInclude;
    EXPECT_THAT(
      ranges,
      ElementsAre(TI{"A_TEXT.h", false, false, "#ifdef FOO"},                //
                  TI{"B_TEXT.h", false, false, "#ifndef BAR"},               //
                  TI{"B_PRIME_TEXT.h", false, true, "#ifndef BAR", "#else"}  //
                  ));
  }
}

TEST(PreprocessUtils, PreprocessRangeIndirectDefinedInclusion) {
  constexpr std::string_view kTestContent = R"deftest(
#if 0
#  define A_PRIME_DEF 0
#else
#  define A_PRIME_DEF 1
#endif
#if A_PRIME_DEF
#include "A_TEXT.h"
#else
#include "B_TEXT.h"
#endif
)deftest";

  {
    DefineMap defs;
    NamedLineIndexedContent scanned_src("<text>", kTestContent);
    auto ranges = ExtractCCIncludes(&scanned_src, defs);
    using TI = TaggedInclude;
    EXPECT_THAT(ranges,
                ElementsAre(TI{"A_TEXT.h", false, false, "#if A_PRIME_DEF"}  //
                            ));
  }
}

TEST(PreprocessUtils, PreprocessRangeIndirectUnDefinedInclusion) {
  constexpr std::string_view kTestContent = R"deftest(
#if 0
#  define FOO 1
#else
#  undef BAR
#endif
#ifdef BAR
# include "WITH_BAR.h"
#else
# include "NO_BAR.h"
#endif
)deftest";

  {
    DefineMap defs;
    defs["BAR"] = true;
    NamedLineIndexedContent scanned_src("<text>", kTestContent);
    auto ranges = ExtractCCIncludes(&scanned_src, defs);
    using TI = TaggedInclude;
    EXPECT_THAT(
      ranges, ElementsAre(TI{"WITH_BAR.h", false, true, "#ifdef BAR"},  //
                          TI{"NO_BAR.h", false, false, "#ifdef BAR", "#else"}));
  }
}

static LineColumn PosOfPart(const NamedLineIndexedContent &src,
                            const std::vector<TaggedInclude> &parts, size_t i) {
  CHECK(i <= parts.size());
  return src.GetLocation(parts[i].include).line_column_range.start;
}

// This is a somewhat older test with a mix of various expectations. The
// tests above are much more to the point. Keeping it for now to make sure
// things don't break.
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
#if THIS_IS_NOT_DEFINED
#  include "ifdefed-out.h"
#endif
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
  const auto includes = ExtractCCIncludes(&scanned_src, DefineMap());
  using TI = TaggedInclude;
  EXPECT_THAT(
    includes,
    ElementsAre(
      /* 0*/ TI{"CaSe-dash_underscore.h", false, false},
      /* 1*/ TI{"a_bracket_include", true, false},
      /* 2*/ TI{"but-this.h", false, false},
      /* 3*/ TI{"with/suffix.hh", false, false},
      /* 4*/ TI{"with/suffix.pb.h", false, false},
      /* 5*/ TI{"with/suffix.inc", false, false},
      /* 6*/ TI{"ifdefed-out.h", false, true, "#if THIS_IS_NOT_DEFINED"},
      /* 7*/ TI{"../dotdot.h", false, false},
      /* 8*/ TI{"more-special-c++.h", false, false},
      /* 9*/ TI{"w/space.h", false, false},
      /*10*/ TI{"should-be-seen.h", false, false},
      /*11*/ TI{"this-as-well.h", false, false}));
  // Let's check some locations.
  EXPECT_EQ(PosOfPart(scanned_src, includes, 0), (LineColumn{2, 10}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 1), (LineColumn{3, 10}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 2), (LineColumn{5, 13}));

  EXPECT_EQ(PosOfPart(scanned_src, includes, 3), (LineColumn{6, 10}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 4), (LineColumn{7, 10}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 5), (LineColumn{8, 10}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 6), (LineColumn{10, 12}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 8), (LineColumn{19, 10}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 9), (LineColumn{21, 15}));
}

TEST(PreprocessUtils, ProtoImportsAreExtracted) {
  constexpr std::string_view kTestProto = R"pb(
    syntax = "proto3"
    ;

package foo;

// import "not/extracted.proto";
import public "path/to/dependency.proto";
import "other/dep.protofoo";

message Bar {
  string name = 1;
}
)pb";
  NamedLineIndexedContent scanned_src("<proto>", kTestProto);
  const auto imports = ExtractProtoImports(&scanned_src);
  EXPECT_THAT(imports,
              ElementsAre("path/to/dependency.proto", "other/dep.protofoo"));
}

TEST(PreprocessUtils, DefinesFromTargets) {
  ParsedProjectTestUtil pp;
  const ParsedBuildFile *const build_file = pp.Add("//sampe", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  defines = [
    "DEF_FOO_D",
    "VAL_BAR_D=0",
    "VAL_BAZ_D=1",
  ],
  copts = [
    "-DDEF_FOO_C",
    "-DVAL_BAR_C=0",
    "-DVAL_BAZ_C=1",
  ],
)
)");
  query::FindTargets(build_file->ast, {"cc_library"},
                     [](const query::Result &cc_lib) {
                       const auto defines = GetDefinesFromTarget(cc_lib, true);
                       EXPECT_THAT(defines, Contains(Pair("DEF_FOO_D", true)));
                       EXPECT_THAT(defines, Contains(Pair("VAL_BAR_D", false)));
                       EXPECT_THAT(defines, Contains(Pair("VAL_BAZ_D", true)));

                       EXPECT_THAT(defines, Contains(Pair("DEF_FOO_C", true)));
                       EXPECT_THAT(defines, Contains(Pair("VAL_BAR_C", false)));
                       EXPECT_THAT(defines, Contains(Pair("VAL_BAZ_C", true)));
                     });
}
}  // namespace
}  // namespace bant
