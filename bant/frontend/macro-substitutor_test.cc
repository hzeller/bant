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

TEST_F(MacroSubstituteTest, BasicTest) {
  SetMacroContent(R"(
some_macro_rule = (
   cc_library(
     name = name,
     deps = ["a", "b", some_dep] + some_list,
   ),    # <- comma, important to parse this as single value tuple
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
  cc_library(
     name = "foobar",
     deps = ["a", "b", "baz"] + ["x", "y", "z"],
  ),
)
)expanded");
  EXPECT_EQ(result.first, result.second);
}
}  // namespace bant
