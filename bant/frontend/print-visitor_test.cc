// bant - Bazel Navigation Tool
// Copyright (C) 2026 Henner Zeller <h.zeller@acm.org>
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

#include "bant/frontend/print-visitor.h"

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "bant/frontend/ast.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/parser.h"
#include "bant/frontend/scanner.h"
#include "bant/util/arena.h"
#include "gtest/gtest.h"

namespace bant {
namespace {

class PrintVisitorTest : public ::testing::Test {
 protected:
  PrintVisitorTest() : arena_(4096) {}

  std::string ParseAndPrint(std::string_view text) {
    NamedLineIndexedContent source("<text>", text);
    Scanner scanner(source);
    Parser parser(&scanner, &arena_, std::cerr);
    bant::List *ast = parser.parse();
    if (!ast || parser.parse_error()) {
      return "<parse error>";
    }

    std::stringstream ss;
    PrintVisitor printer(ss);
    EXPECT_EQ(ast->size(), 1u);  // We only send one construct to this function.
    ast->at(0)->Accept(&printer);

    // We don't care about the neat formatting with newlines and stuff, just
    // want to make sure we got the expression back properly. Remove multiple
    // spaces in a row.
    std::string result;
    for (std::string token; ss >> token; result.append(" ")) {
      result.append(token);
    }
    result.pop_back();
    return result;
  }

 private:
  Arena arena_;
};

TEST_F(PrintVisitorTest, BasicMathPrecedence) {
  // Should retain parens around lower precedence operators
  EXPECT_EQ(ParseAndPrint("A = (a + b) * c"), "A = (a + b) * c");

  // Should drop unnecessary parens
  EXPECT_EQ(ParseAndPrint("A = (a * b) + c"), "A = a * b + c");
  EXPECT_EQ(ParseAndPrint("A = a * (b + c)"), "A = a * (b + c)");

  // Same precedence: left-associative operators
  EXPECT_EQ(ParseAndPrint("A = (a - b) - c"), "A = a - b - c");
  EXPECT_EQ(ParseAndPrint("A = a - (b - c)"), "A = a - (b - c)");
  EXPECT_EQ(ParseAndPrint("A = a / (b / c)"), "A = a / (b / c)");
}

TEST_F(PrintVisitorTest, UnaryPrecedence) {
  // Unary has higher precedence than binary minus
  EXPECT_EQ(ParseAndPrint("A = -(a - b)"), "A = -(a - b)");
  EXPECT_EQ(ParseAndPrint("A = (-a) - b"), "A = -a - b");

  // Unary not
  EXPECT_EQ(ParseAndPrint("A = not (a == b)"), "A = not a == b");
  EXPECT_EQ(ParseAndPrint("A = (not a) == b"),
            "A = (not a) == b");  // 'not' has lower precedence than '=='
}

TEST_F(PrintVisitorTest, ArrayAccessPrecedence) {
  // Array access [] is precedence 1
  EXPECT_EQ(ParseAndPrint("A = (a + b)[0]"), "A = (a + b)[0]");
  EXPECT_EQ(ParseAndPrint("A = a + (b[0])"), "A = a + b[0]");
  EXPECT_EQ(ParseAndPrint("A = a[b + c]"), "A = a[b + c]");

  // Dot access
  EXPECT_EQ(ParseAndPrint("A = (a + b).c"), "A = (a + b).c");
  EXPECT_EQ(ParseAndPrint("A = a + (b.c)"), "A = a + b.c");
}

TEST_F(PrintVisitorTest, TernaryPrecedence) {
  // Ternary is lower precedence than binary ops
  EXPECT_EQ(ParseAndPrint("A = a + (b if c else d)"),
            "A = a + (b if c else d)");
  EXPECT_EQ(ParseAndPrint("A = (a + b) if c else d"), "A = a + b if c else d");

  // Ternary in right-associative positions
  EXPECT_EQ(ParseAndPrint("A = a if b else (c if d else e)"),
            "A = a if b else c if d else e");
  EXPECT_EQ(ParseAndPrint("A = (a if b else c) if d else e"),
            "A = (a if b else c) if d else e");

  // Condition
  EXPECT_EQ(ParseAndPrint("A = a if (b if c else d) else e"),
            "A = a if (b if c else d) else e");
}

TEST_F(PrintVisitorTest, BitwisePrecedence) {
  // Bitwise OR (6) is lower precedence than Shift (5)
  EXPECT_EQ(ParseAndPrint("A = a | (b << c)"), "A = a | b << c");
  EXPECT_EQ(ParseAndPrint("A = (a | b) << c"), "A = (a | b) << c");

  // Shift (5) is lower precedence than Plus (4)
  EXPECT_EQ(ParseAndPrint("A = a << (b + c)"), "A = a << b + c");
  EXPECT_EQ(ParseAndPrint("A = (a << b) + c"), "A = (a << b) + c");

  // Left-associative tests
  EXPECT_EQ(ParseAndPrint("A = a | (b | c)"), "A = a | (b | c)");
  EXPECT_EQ(ParseAndPrint("A = (a << b) << c"), "A = a << b << c");
}

TEST_F(PrintVisitorTest, LogicalPrecedence) {
  // OR (10) is lower than AND (9)
  EXPECT_EQ(ParseAndPrint("A = a or (b and c)"), "A = a or b and c");
  EXPECT_EQ(ParseAndPrint("A = (a or b) and c"), "A = (a or b) and c");

  // AND (9) is lower than NOT (8)
  EXPECT_EQ(ParseAndPrint("A = a and not b"), "A = a and not b");
  EXPECT_EQ(ParseAndPrint("A = not (a and b)"), "A = not (a and b)");

  // Left-associative tests
  EXPECT_EQ(ParseAndPrint("A = (a and b) and c"), "A = a and b and c");
  EXPECT_EQ(ParseAndPrint("A = a and (b and c)"), "A = a and (b and c)");
}

TEST_F(PrintVisitorTest, ListComprehension) {
  EXPECT_EQ(ParseAndPrint("A = [x for x in y]"), "A = [x for (x,) in y]");
  EXPECT_EQ(ParseAndPrint("A = {k: v for k, v in d if k > 0}"),
            "A = {k : v for ( k, v ) in d if k > 0}");

  // Nested loops and conditions
  EXPECT_EQ(
    ParseAndPrint("A = [x * y for x in a if x > 0 for y in b if y < 0]"),
    "A = [x * y for (x,) in a if x > 0 for (y,) in b if y < 0]");
}

TEST_F(PrintVisitorTest, ListsAndTuples) {
  // Empty lists
  EXPECT_EQ(ParseAndPrint("A = []"), "A = []");
  EXPECT_EQ(ParseAndPrint("A = {}"), "A = {}");

  // Single element tuple must have trailing comma
  EXPECT_EQ(ParseAndPrint("A = (1,)"), "A = (1,)");

  // Single element list/dict doesn't need trailing comma
  EXPECT_EQ(ParseAndPrint("A = [1]"), "A = [1]");
  EXPECT_EQ(ParseAndPrint("A = {a: b}"), "A = {a : b}");

  // Multiple elements
  EXPECT_EQ(ParseAndPrint("A = [1, 2, 3]"), "A = [ 1, 2, 3 ]");
  EXPECT_EQ(ParseAndPrint("A = (1, 2, 3)"), "A = ( 1, 2, 3 )");
  EXPECT_EQ(ParseAndPrint("A = {a: b, c: d}"), "A = { a : b, c : d }");
}

TEST_F(PrintVisitorTest, FunctionCalls) {
  EXPECT_EQ(ParseAndPrint("A = foo()"), "A = foo()");
  EXPECT_EQ(ParseAndPrint("A = foo(1)"), "A = foo(1,)");
  EXPECT_EQ(ParseAndPrint("A = foo(1, 2, 3)"), "A = foo( 1, 2, 3 )");

  // Function calls as part of a chain
  EXPECT_EQ(ParseAndPrint("A = foo(a).bar(b)"), "A = foo(a,).bar(b,)");
  EXPECT_EQ(ParseAndPrint("A = foo(a)[0]"), "A = foo(a,)[0]");
}

}  // namespace
}  // namespace bant
