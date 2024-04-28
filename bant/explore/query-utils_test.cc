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

#include "bant/explore/query-utils.h"

#include "bant/frontend/parsed-project_testutil.h"
#include "bant/frontend/project-parser.h"
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
}  // namespace bant::query
