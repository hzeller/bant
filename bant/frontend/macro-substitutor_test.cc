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

#include "bant/frontend/macro-substitutor.h"

#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "bant/frontend/ast.h"
#include "bant/frontend/parsed-project_testutil.h"
#include "bant/session.h"
#include "gtest/gtest.h"

namespace bant {
// TODO: the Elaborator test also has something similar. Unify ?
class MacroSubstituteTest : public testing::Test {
 public:
  std::pair<std::string, std::string> MacroSubstituteAndPrint(
    std::string_view to_substitute, std::string_view expected) {
    const CommandlineFlags flags = CommandlineFlags{.verbose = 1};
    const auto &substitute_parsed = pp_.Add("//substitute", to_substitute);

    Session session(&std::cerr, &std::cerr, flags);
    const std::string sub_print = ToString(
      MacroSubstitute(session, &pp_.project(), substitute_parsed->ast));

    // Parse and re-print expected to get same formatting.
    const std::string expect_print =
      ToString(pp_.Add("//expect", expected)->ast);

    return {sub_print, expect_print};
  }

  void SetMacroContent(std::string_view macros) { pp_.SetMacroContent(macros); }

 private:
  ParsedProjectTestUtil pp_;
};

TEST_F(MacroSubstituteTest, MacroBodyIsFunCall) {
  SetMacroContent(R"(
some_macro_rule = cc_library(
     name = name,
     deps = ["a", "b", some_dep] + some_list,
   )
)");

  const auto result = MacroSubstituteAndPrint(R"input(
some_macro_rule(
   name = "foobar",
   some_dep = "baz",
   some_list = [ "x", "y", "z" ],
)
)input",
                                              R"expanded(
cc_library(
    name = "foobar",
    deps = ["a", "b", "baz"] + ["x", "y", "z"],
)
)expanded");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(MacroSubstituteTest, MacroBodyIsTuple) {
  SetMacroContent(R"(
some_macro_rule = (
   genrule(name = name + "-gen"),
   cc_library(
     name = name,
     deps = ["a", "b", some_dep] + some_list,
   ),
)
)");

  const auto result = MacroSubstituteAndPrint(R"input(
some_macro_rule(
   name = "foobar",
   some_dep = "baz",
   some_list = [ "x", "y", "z" ],
)
)input",
                                              R"expanded(
( # Expanded: is tuple
  genrule(name = "foobar" + "-gen"),
  cc_library(
     name = "foobar",
     deps = ["a", "b", "baz"] + ["x", "y", "z"],
  ),
)
)expanded");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(MacroSubstituteTest, MacroBodyForwardKwArgsFunction) {
  SetMacroContent(R"(
some_macro_rule = bant_forward_args(
    cc_library(
      visibility = "//visibility:public",
    )
  )
)");

  const auto result = MacroSubstituteAndPrint(R"input(
some_macro_rule(
   name = "foobar",
   deps = ["baz"],
)
)input",
                                              R"expanded(
cc_library(
    # Original parameters passed in
    name = "foobar",
    deps = ["baz"],
    visibility = "//visibility:public",
)
)expanded");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(MacroSubstituteTest, MacroBodyForwardKwArgsToMultipleFunctionsInTuple) {
  SetMacroContent(R"(
some_macro_rule = bant_forward_args(
      cc_library(
        visibility = "//visibility:public",
        stop_expansion = foo(),
      ),
      another_rule(
        answer = 42,
      )
   )
)");

  const auto result = MacroSubstituteAndPrint(R"input(
some_macro_rule(
   name = "foobar",   # These will be forwarded to any fun calls found inside
   deps = ["baz"],
)
)input",
                                              R"expanded(
(  # <- Expansion is a tuple as it has multiple elements
cc_library(
    # Original parameters passed in
    name = "foobar",
    deps = ["baz"],
    visibility = "//visibility:public",
    stop_expansion = foo(),
),
another_rule(
    name = "foobar",
    deps = ["baz"],
    answer = 42,
),
)
)expanded");

  EXPECT_EQ(result.first, result.second);
}

}  // namespace bant
