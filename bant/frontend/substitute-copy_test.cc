// bant - Bazel Navigation Tool
// Copyright (C) 2025 Henner Zeller <h.zeller@acm.org>
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

#include "bant/frontend/substitute-copy.h"

#include <iostream>
#include <string_view>

#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/parser.h"
#include "bant/frontend/scanner.h"
#include "bant/util/arena.h"
#include "gtest/gtest.h"

namespace bant {

class VariableSubstituteCopyTest : public ::testing::Test {
 protected:
  bant::List *Parse(std::string_view text) {
    NamedLineIndexedContent source("<text>", text);
    Scanner scanner(source);
    Parser parser(&scanner, &arena_, std::cerr);
    bant::List *result = parser.parse();
    EXPECT_FALSE(parser.parse_error()) << text;
    return result;
  }

  Arena *arena() { return &arena_; }

 private:
  Arena arena_{4096};
};

TEST_F(VariableSubstituteCopyTest, TestVarReplacement) {
  constexpr std::string_view kOriginal = R"(
foo(
  name = var1,
  other = var2 + 7,
  other = no_valid_variable,
)
)";

  List *const original = Parse(kOriginal);

  {
    const query::KwMap no_vars;
    Node *no_substitue = VariableSubstituteCopy(original, arena(), no_vars);
    EXPECT_EQ(original, no_substitue);  // No variables, no new nodes.
  }

  {
    query::KwMap vars;
    vars.emplace("var1", IntScalar::FromLiteral(arena(), "42"));
    vars.emplace("var2", IntScalar::FromLiteral(arena(), "123"));
    Node *with_substitue = VariableSubstituteCopy(original, arena(), vars);
    EXPECT_NE(original, with_substitue) << "Expected var substitute";

    constexpr std::string_view kExpected = R"(
foo(
  name = 42,
  other = 123 + 7,
  other = no_valid_variable,
)
)";
    EXPECT_EQ(ToString(with_substitue), ToString(Parse(kExpected)));
  }

  // And we also expect that the original AST has not been messed with.
  EXPECT_EQ(ToString(original), ToString(Parse(kOriginal)));
}
}  // namespace bant
