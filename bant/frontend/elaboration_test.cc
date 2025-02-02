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

#include "bant/frontend/elaboration.h"

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/parsed-project_testutil.h"
#include "bant/session.h"
#include "gtest/gtest.h"

namespace bant {

class ElaborationTest : public testing::Test {
 protected:
  std::pair<std::string, std::string> ElabAndPrint(
    std::string_view to_elaborate, std::string_view expected,
    const CommandlineFlags &flags = CommandlineFlags{.verbose = 1}) {
    elaborated_ = pp_.Add("//elab", to_elaborate);
    const ParsedBuildFile *expected_parsed = pp_.Add("//expected", expected);

    Session session(&std::cerr, &std::cerr, flags);
    std::stringstream elab_print;
    elab_print << bant::Elaborate(session, &pp_.project(), elaborated_->package,
                                  elaborated_->ast);

    std::stringstream expect_print;
    expect_print << expected_parsed->ast;
    return {elab_print.str(), expect_print.str()};
  }

  // Returns the last elaborated file.
  const ParsedBuildFile *elaborated() { return elaborated_; }

  ParsedProject &project() { return pp_.project(); }

 private:
  ParsedProjectTestUtil pp_;
  const ParsedBuildFile *elaborated_ = nullptr;
};

TEST_F(ElaborationTest, ExpandVariables) {
  auto result = ElabAndPrint(
    R"(
BAR = "bar.cc"
BAR_REF = BAR        # let's create a couple of indirections
SOURCES = ["foo.cc", BAR_REF]

cc_library(
  name = "foo",
  srcs = SOURCES,    # global variable SOURCES should be expanded
  baz = name,        # nested symbol 'name' should not be expanded
)
)",
    R"(
BAR = "bar.cc"
BAR_REF = "bar.cc"   # ... indirections resolved
SOURCES = ["foo.cc", "bar.cc"]

cc_library(
  name = "foo",
  srcs = ["foo.cc", "bar.cc"],  # <- expanded
  baz = name,                   # <- not expanded
)
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, ConcatLists) {
  auto result = ElabAndPrint(
    R"(
FOO = ["baz.cc", "qux.cc"]
cc_library(
  name = "foo",
  srcs = [ "foo.cc" ] + [ "bar.cc" ] + FOO
)
)",
    R"(
FOO = ["baz.cc", "qux.cc"]
cc_library(
  name = "foo",
  srcs = [ "foo.cc", "bar.cc", "baz.cc", "qux.cc" ],
)
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, ConcatListWithUndefinedValue) {
  auto result = ElabAndPrint(
    R"(
# UNDEFINED_VALUE
cc_library(
  name = "foo",
  srcs = [ "foo.cc" ] + UNDEFINED + [ "bar.cc" ],
)
)",
    R"(
cc_library(
  name = "foo",
  srcs = [ "foo.cc", "bar.cc" ]    # best effort result
)
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, SelectChoosesConditionDefault) {
  auto result = ElabAndPrint(
    R"(
cc_library(
  name = "foo",
  srcs = select({
     "//:foo"               : ["abc.cc"],
     [ "not-a-string"]      : ["baz.cc"],
     "//conditions:default" : ["def.cc"],
   })
)
)",
    R"(
cc_library(
  name = "foo",
  srcs = ["def.cc"]   # No condition set, choosing default
)
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, SelectWithChosenOption) {
  CommandlineFlags flags = CommandlineFlags{.verbose = 1};
  flags.custom_flags.emplace("//:foo");
  auto result = ElabAndPrint(
    R"(
cc_library(
  name = "foo",
  srcs = select({
     "//:foo"               : ["abc.cc"],
     [ "not-a-string"]      : ["baz.cc"],
     "//conditions:default" : ["def.cc"],
   })
)
)",
    R"(
cc_library(
  name = "foo",
  srcs = ["abc.cc"]
)
)",
    flags);

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, ConcatStrings) {
  auto result = ElabAndPrint(
    R"(
BAZ = "baz"
cc_library(
  name = "foo" + "bar" + BAZ,
  include_prefix = ("foo" + "bar") + "qux",
)
)",
    R"(
BAZ = "baz"
cc_library(
  name = "foobarbaz",
  include_prefix = "foobarqux",
)
)");

  EXPECT_EQ(result.first, result.second);

  // Let's see that the 'location' of the assembled string points to the
  // original '+' operand.
  //
  // Unlike one would expect intuitively, the way the parser currently handles
  // '+' is right-associative, so evaluation happens from right to left:
  // "foo" + ("bar" + BAZ)).
  // Since string-concat is assoicative that is a perfectly fine.
  // But this is why the first, not last, '+' is shown as file location.
  query::FindTargets(elaborated()->ast, {}, [&](const query::Result &result) {
    EXPECT_EQ(result.name, "foobarbaz");
    EXPECT_EQ(project().Loc(result.name), "//elab/BUILD:4:16:");

    // Parenthesis around second expression: Second plus is 'location'
    EXPECT_EQ(result.include_prefix, "foobarqux");
    EXPECT_EQ(project().Loc(result.include_prefix), "//elab/BUILD:5:36:");
  });
}
}  // namespace bant
