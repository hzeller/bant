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
#include "bant/frontend/elaboration.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/parsed-project_testutil.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "gtest/gtest.h"

namespace bant {
namespace {
// TODO: the Elaborator test also has something similar. Unify ?
class MacroSubstituteTest : public ::testing::Test {
 public:
  std::pair<std::string, std::string> MacroSubstituteAndPrint(
    std::string_view to_substitute, std::string_view expected,
    bool elab = false) {
    const CommandlineFlags flags = CommandlineFlags{.verbose = 1};
    const auto &substitute_parsed = pp_.Add("//substitute", to_substitute);

    Session session(&std::cerr, &std::cerr, &std::cerr, flags);
    Node *macro_substited =
      MacroSubstitute(session, &pp_.project(), {}, substitute_parsed->ast);
    if (elab) {
      macro_substited =
        Elaborate(session, &pp_.project(), {}, {}, macro_substited);
    }
    const std::string sub_print = ToString(macro_substited);

    // Parse and re-print expected to get same formatting.
    const std::string expect_print =
      ToString(pp_.Add("//expect", expected)->ast);

    return {expect_print, sub_print};
  }

  void SetBuiltinMacros(std::string_view macros) {
    pp_.SetMacroContent(macros);
  }

  void SetPackageMacros(std::string_view macros) {
    pp_.SetPackageMacros({}, macros);
  }

 private:
  ParsedProjectTestUtil pp_;
};

TEST_F(MacroSubstituteTest, MacroBodyIsFunCall) {
  SetBuiltinMacros(R"(
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

TEST_F(MacroSubstituteTest, MacroBodyEvaluatesListComprehension) {
  SetBuiltinMacros(R"(
some_macro_rule = [
   foo(name = "generated-{}".format(x))
   for x in macro_parameter
]
)");

  const auto result = MacroSubstituteAndPrint(R"input(
SOME_LIST=["a", "b", "c"]
some_macro_rule(macro_parameter = SOME_LIST)
some_macro_rule(macro_parameter = ["d", "e", "f"])
)input",
                                              R"expanded(
SOME_LIST=["a", "b", "c"]
[
  foo(name = "generated-a"),
  foo(name = "generated-b"),
  foo(name = "generated-c"),
]

[
  foo(name = "generated-d"),
  foo(name = "generated-e"),
  foo(name = "generated-f"),
]
)expanded",
                                              /* elab= */ true);

  EXPECT_EQ(result.first, result.second);
}

TEST_F(MacroSubstituteTest, MacroBodyIsTuple) {
  SetBuiltinMacros(R"(
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

TEST_F(MacroSubstituteTest, UsePositionalArgs) {
  SetBuiltinMacros(R"(
dict = {k : v for (k, v) in _arg_0}
)");

  const auto result = MacroSubstituteAndPrint(R"input(
A = dict([ ("foo", 1), ("bar", 42) ])
B = dict()
)input",
                                              R"expanded(
A = {k : v for (k, v) in [ ("foo", 1), ("bar", 42) ]}
B = {k : v for (k, v) in _arg_0}
)expanded");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(MacroSubstituteTest, MacroBodyForwardKwArgsFunction) {
  SetBuiltinMacros(R"(
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
  SetBuiltinMacros(R"(
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

TEST_F(MacroSubstituteTest, ProjectLocalMacroForwardArgs) {
  // Simulate a project-local macro like my_cc_test =
  // bant_forward_args(cc_test())
  SetBuiltinMacros(R"(
my_cc_test = bant_forward_args(cc_test())
)");

  const auto result = MacroSubstituteAndPrint(R"input(
my_cc_test(
   name = "my_test",
   deps = ["//some:lib"],
)
)input",
                                              R"expanded(
cc_test(
    name = "my_test",
    deps = ["//some:lib"],
)
)expanded");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(MacroSubstituteTest, MultipleSetMacroContentCallsAreAdditive) {
  // First call: define one macro
  SetBuiltinMacros(R"(
first_rule = bant_forward_args(cc_library())
)");

  // Second call: define another macro (should not crash, should be additive)
  SetBuiltinMacros(R"(
second_rule = bant_forward_args(cc_test())
)");

  // Both macros should work
  const auto result1 = MacroSubstituteAndPrint(R"input(
first_rule(
   name = "lib1",
)
)input",
                                               R"expanded(
cc_library(
    name = "lib1",
)
)expanded");
  EXPECT_EQ(result1.first, result1.second);

  const auto result2 = MacroSubstituteAndPrint(R"input(
second_rule(
   name = "test1",
)
)input",
                                               R"expanded(
cc_test(
    name = "test1",
)
)expanded");
  EXPECT_EQ(result2.first, result2.second);
}

TEST_F(MacroSubstituteTest, ProjectLocalMacroOverridesBuiltin) {
  // First call: define a macro
  SetBuiltinMacros(R"(
my_rule = bant_forward_args(cc_library())
)");

  // Second call: override same macro name
  SetPackageMacros(R"(
my_rule = bant_forward_args(cc_test())
)");

  // Should use the latest definition (cc_test, not cc_library)
  const auto result = MacroSubstituteAndPrint(R"input(
my_rule(
   name = "foo",
)
)input",
                                              R"expanded(
cc_test(
    name = "foo",
)
)expanded");
  EXPECT_EQ(result.first, result.second);
}

TEST_F(MacroSubstituteTest, BuiltinMacrosAreParsing) {
  // Just instantiating a project with builtins enabled to see if they
  // parse properly.
  const ParsedProject project({}, false, /*with_builtin_macros =*/true);
}
}  // namespace
}  // namespace bant
