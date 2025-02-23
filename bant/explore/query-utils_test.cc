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

#include "bant/explore/query-utils.h"

#include "bant/frontend/parsed-project.h"
#include "bant/frontend/parsed-project_testutil.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ElementsAre;

namespace bant::query {
TEST(QueryUtils, BasicQuery) {
  ParsedProjectTestUtil pp;
  const ParsedBuildFile *build_file = pp.Add("//", R"(
cc_library(
  name = "foo_lib",
  srcs = ["foo.cc", "bar.cc"],
  hdrs = ["foo.h"]
)
)");
  EXPECT_TRUE(build_file);
  FindTargets(build_file->ast, {"cc_library"}, [](const query::Result &found) {
    EXPECT_EQ(found.name, "foo_lib");
    EXPECT_THAT(ExtractStringList(found.srcs_list),
                ElementsAre("foo.cc", "bar.cc"));
    EXPECT_THAT(ExtractStringList(found.hdrs_list), ElementsAre("foo.h"));
  });
}

TEST(QueryUtils, VisibilityOnRule) {
  ParsedProjectTestUtil pp;
  const ParsedBuildFile *build_file = pp.Add("//", R"(
cc_library(
  name = "foo_lib",
  visibility = ["//foo:__pkg__"],
)
cc_library(
  name = "bar_lib",
)
)");
  EXPECT_TRUE(build_file);
  FindTargets(build_file->ast, {"cc_library"}, [](const query::Result &found) {
    if (found.name == "foo_lib") {
      EXPECT_THAT(ExtractStringList(found.visibility),
                  ElementsAre("//foo:__pkg__"));
    }
    if (found.name == "bar_lib") {
      EXPECT_TRUE(found.visibility == nullptr);
    }
  });
}

TEST(QueryUtils, VisibilityFallbackToDefaultVisibility) {
  ParsedProjectTestUtil pp;
  const ParsedBuildFile *build_file = pp.Add("//", R"(
package(
  default_visibility = ["//visibility:private"],
)

cc_library(
  name = "foo_lib",
  visibility = ["//foo:__pkg__"],
)
cc_library(
  name = "bar_lib",
)
)");
  EXPECT_TRUE(build_file);
  FindTargets(build_file->ast, {"cc_library"}, [](const query::Result &found) {
    if (found.name == "foo_lib") {
      EXPECT_THAT(ExtractStringList(found.visibility),
                  ElementsAre("//foo:__pkg__"));
    }
    if (found.name == "bar_lib") {
      EXPECT_THAT(ExtractStringList(found.visibility),
                  ElementsAre("//visibility:private"));
    }
  });
}

TEST(QueryUtils, ExtractKWArg) {
  ParsedProjectTestUtil pp;
  const ParsedBuildFile *build_file = pp.Add("//", R"(
foo(
  name = "bar",
  "hello",                         # Don't trip over non kw-args
  flub = "baz",
  flob = "foobar",
  flab = [ "this", "is", "a", "list"],
)
)");
  EXPECT_TRUE(build_file);
  FindTargets(build_file->ast, {"foo"}, [](const query::Result &found) {
    EXPECT_EQ(found.name, "bar");
    auto kw_arg_value = FindKWArgAsStringView(found.node, "doesnotexist");
    EXPECT_FALSE(kw_arg_value.has_value());

    kw_arg_value = FindKWArgAsStringView(found.node, "flub");
    EXPECT_TRUE(kw_arg_value.has_value());
    EXPECT_EQ(*kw_arg_value, "baz");

    kw_arg_value = FindKWArgAsStringView(found.node, "flob");
    EXPECT_TRUE(kw_arg_value.has_value());
    EXPECT_EQ(*kw_arg_value, "foobar");

    kw_arg_value = FindKWArgAsStringView(found.node, "flab");
    EXPECT_FALSE(kw_arg_value.has_value());  // a list, not a string.
  });
}

}  // namespace bant::query
