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

#include "parser.h"

#include <initializer_list>
#include <sstream>
#include <string_view>
#include <utility>

#include "arena.h"
#include "ast.h"
#include "gtest/gtest.h"

namespace bant {
class ParserTest : public testing::Test {
 protected:
  ParserTest() : arena_(4096) {}

  bant::List *Parse(std::string_view text) {
    LineColumnMap lc;
    Scanner scanner(text, &lc);
    Parser parser(&scanner, &arena_, "<text>", std::cerr);
    return parser.parse();
  }

  // Some helpers to build ASTs to compre
  StringScalar *Str(std::string_view s, bool raw = false) {
    return arena_.New<StringScalar>(s, raw);
  }
  IntScalar *Int(int num) {
    return arena_.New<IntScalar>(num);
  }
  Identifier *Id(std::string_view i) { return arena_.New<Identifier>(i); }
  BinOpNode *Op(char op, Node *a, Node *b) {
    return arena_.New<BinOpNode>(a, b, op);
  }
  Assignment *Assign(std::string_view id, Node *b) {
    return arena_.New<Assignment>(Id(id), b);
  }
  FunCall *Call(std::string_view id, List *args) {
    return arena_.New<FunCall>(Id(id), args);
  }

  bant::List *List(std::initializer_list<Node *> elements) {
    bant::List *result = arena_.New<bant::List>(List::Type::kList);
    for (Node *n : elements) result->Append(&arena_, n);
    return result;
  }

  bant::List *Tuple(std::initializer_list<Node *> elements) {
    bant::List *result = arena_.New<bant::List>(bant::List::Type::kTuple);
    for (Node *n : elements) result->Append(&arena_, n);
    return result;
  }
  bant::List *Map(
    std::initializer_list<std::pair<Node *, Node *>> elements) {
    bant::List *result = arena_.New<bant::List>(List::Type::kMap);
    for (const auto &n : elements) {
      result->Append(&arena_, Op(':', n.first, n.second));
    }
    return result;
  }

  bant::ListComprehension *ListComprehension(bant::List *pattern,
                                             bant::List *vars, Node *source) {
    return arena_.New<bant::ListComprehension>(pattern, vars, source);
  }

  std::string Print(Node *n) {
    std::stringstream s;
    s << n;
    return s.str();
  }

 private:
  Arena arena_;
};

TEST_F(ParserTest, Assignments) {
  Node *const expected = List({
      Assign("foo", Str("regular_string")),
      Assign("bar", Str("raw_string", true)),
    });
  EXPECT_EQ(Print(expected), Print(Parse(R"(
foo = "regular_string"
bar = r"raw_string"
)")));
}

// A typical Pythonism.
TEST_F(ParserTest, CallOnString) {
  Node *const expected = List({
    Call("funcall", Tuple({Op('.', Str("Some {} str"),
                              Call("format", Tuple({Str("baz")})))})),
  });

  EXPECT_EQ(Print(expected), Print(Parse(R"(
funcall("Some {} str".format("baz"))
)")));
}

TEST_F(ParserTest, ParenthizedExpressions) {
  Node *const expected = List({
    Assign("foo", Op('+', Str("a"), Str("b"))),
    Assign("fmt", Op('%', Str("a%s"), Str("b"))),
    Assign("bar", Op('+', Str("a"), Str("b"))),
    Assign("baz", Op('+', Str("a"), Str("b"))),
  });

  EXPECT_EQ(Print(expected), Print(Parse(R"(
foo = "a" + "b"
fmt = "a%s" % "b"
bar = ("a" + "b")
baz = (((("a" + "b"))))
)")));
}

TEST_F(ParserTest, EmptyTuple) {
  Node *const expected = List({
    Assign("empty", Tuple({})),
  });

  EXPECT_EQ(Print(expected), Print(Parse(R"(
empty = ()
)")));
}

TEST_F(ParserTest, TupleExpressions) {
  Node *const expected = List({
    Assign("empty", Tuple({})),
    Assign("foo", Tuple({Str("a"), Str("b"), Str("c")})),
    Assign("bar", Tuple({Str("a"), Str("b")})),
    Assign("baz", Tuple({Str("a")})), Assign("qux", Str("a")),
    Assign("buz", Tuple({Str("a")})),  // like baz
  });

  EXPECT_EQ(Print(expected), Print(Parse(R"(
empty = ()
foo = ("a", "b", "c")
bar = ("a", "b")
baz = ("a",)    # Comma diffentiates between paren-expr and tuple
qux = ("a")     # ... this is just a parenthized expression
buz = (("a",))  # parenthized expression that contains a single tuple.
)")));
}

TEST_F(ParserTest, MapAssign) {
  Node *const expected = List({
      Assign("str_map", Map({ { Str("orange"), Str("fruit") } })),
      Assign("num_map", Map({ { Str("answer"), Int(42) } })),
      Assign("id_map", Map({ { Id("SOME_IDENTIFIER"), Id("ANOTHER_ID") }}))
    });
  EXPECT_EQ(Print(expected), Print(Parse(R"(
str_map = { "orange" : "fruit" }
num_map = { "answer" : 42 }
id_map = { SOME_IDENTIFIER : ANOTHER_ID }
)")));
}

TEST_F(ParserTest, SimpleFunctionCalls) {
  Node *const expected = List({
    Call("foo", Tuple({Str("foo"), Id("k")})),
    Op('.', Id("nested"), Call("bar", Tuple({Str("baz"), Id("m")}))),
    Call("baz", Tuple({})),
  });

  EXPECT_EQ(Print(expected), Print(Parse(R"(
foo("foo", k)
nested.bar("baz", m)
baz()
)")));
}

TEST_F(ParserTest, ParseFunctionCall) {
  Node *const expected = List({
    Call("foo", Tuple({Str("x"), Str("y")})),
    List({Call("bar", Tuple({Str("a")}))}),
  });

  // Also testing fancy string literal.
  EXPECT_EQ(Print(expected), Print(Parse(R"(
foo("x", """y""")  # Triple quoted-string should look like regular one.
[bar("a")]         # function call result as a value inside a list
)")));
}

TEST_F(ParserTest, ParseListComprehension) {
  Node *const expected = List({ListComprehension(
    Tuple({Op('+', Str("foo"), Id("i"))}),    // apply these expressions
    List({Id("i")}),                          // with this list of variables
    List({Str("a"), Str("b"), Str("c")}))});  // over this content

  EXPECT_EQ(Print(expected), Print(Parse(R"(
  [
     ("foo" + i,)  # Comma helps identify this as tuple expression
     for i in ["a", "b", "c"]
  ]
)")));
}

TEST_F(ParserTest, ParseTernary) {
  Node *n = Parse("[foo() if a + b else baz()]");
  EXPECT_EQ(Print(n), "[[foo()\n         if a + b else baz()\n        ]]");
}
}  // namespace bant
