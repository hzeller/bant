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

#include "bant/frontend/elaboration.h"

#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "bant/frontend/parsed-project.h"
#include "bant/frontend/parsed-project_testutil.h"
#include "gtest/gtest.h"

namespace bant {

static std::pair<std::string, std::string> ElabAndPrint(
  std::string_view to_elaborate, std::string_view expected) {
  ParsedProjectTestUtil pp;
  const ParsedBuildFile *elab_parsed = pp.Add("//elab", to_elaborate);
  const ParsedBuildFile *expected_parsed = pp.Add("//expected", expected);

  std::stringstream elab_print;
  elab_print << bant::Elaborate(&pp.project(), elab_parsed->ast);

  std::stringstream expect_print;
  expect_print << expected_parsed->ast;
  return {elab_print.str(), expect_print.str()};
}

TEST(ElaborationTest, ExpandVariables) {
  auto result = ElabAndPrint(
    R"(
BAR = "bar.cc"
BAR_REF = BAR        # let's create a couple of indirections
SOURCES = ["foo.cc", BAR_REF]

cc_library(
  name = "foo",
  srcs = SOURCES,    # global variable SOURCES should be expanded
  baz = name,        # nested symbol 'name' should not be expanded
))",
    R"(
BAR = "bar.cc"
BAR_REF = "bar.cc"   # ... indirections resolved
SOURCES = ["foo.cc", "bar.cc"]

cc_library(
  name = "foo",
  srcs = ["foo.cc", "bar.cc"],  # <- expanded
  baz = name,                   # <- not expanded
))");

  EXPECT_EQ(result.first, result.second);
}
}  // namespace bant
