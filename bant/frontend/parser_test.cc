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

#include "bant/frontend/parser.h"

#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/die_if_null.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/scanner.h"
#include "bant/util/arena.h"
#include "gtest/gtest.h"

namespace bant {
class ParserTest : public testing::Test {
 protected:
  ParserTest() : arena_(4096) {}

  bant::List *Parse(std::string_view text) {
    NamedLineIndexedContent source("<text>", text);
    Scanner scanner(source);
    Parser parser(&scanner, &arena_, std::cerr);
    bant::List *first_pass = parser.parse();
    RoundTripPrintParseAgainTest(first_pass);
    return first_pass;
  }

  // Some helpers to build ASTs to compare
  StringScalar *Str(std::string_view s, bool triple = false, bool raw = false) {
    return arena_.New<StringScalar>(s, triple, raw);
  }
  IntScalar *Int(int num) { return arena_.New<IntScalar>("", num); }
  IntScalar *Number(std::string_view fancy_literal) {
    return IntScalar::FromLiteral(&arena_, fancy_literal);
  }
  Identifier *Id(std::string_view i) { return arena_.New<Identifier>(i); }
  BinOpNode *Op(char op, Node *a, Node *b) {
    return Op(static_cast<TokenType>(op), a, b);
  }
  BinOpNode *Op(TokenType op, Node *a, Node *b) {
    return arena_.New<BinOpNode>(a, b, op, "");
  }
  BinOpNode *In(Node *a, Node *b) { return Op(TokenType::kIn, a, b); }
  BinOpNode *NotIn(Node *a, Node *b) { return Op(TokenType::kNotIn, a, b); }
  BinOpNode *For(Node *subject, BinOpNode *in_expr) {
    return Op(TokenType::kFor, subject, in_expr);
  }

  UnaryExpr *UnaryOp(TokenType op, Node *n) {
    return arena_.New<UnaryExpr>(op, n);
  }
  Assignment *Assign(std::string_view id, Node *b) {
    return arena_.New<Assignment>(Id(id), b, "");
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
  bant::List *Map(std::initializer_list<std::pair<Node *, Node *>> elements) {
    bant::List *result = arena_.New<bant::List>(List::Type::kMap);
    for (const auto &n : elements) {
      result->Append(&arena_, Op(':', n.first, n.second));
    }
    return result;
  }

  bant::ListComprehension *ListComprehension(List::Type type,
                                             BinOpNode *for_expr) {
    return arena_.New<bant::ListComprehension>(type, for_expr);
  }

  static std::string Print(Node *n) {
    std::stringstream s;
    s << n;
    return s.str();
  }

 private:
  // Roundtrip test.
  // Print each element we got into a string, re-parse, re-print
  // and make sure we get the same.
  //
  // Somewhat orthogonal of the parse testing we do here,
  // but since this will run through all kinds of parsing situations,
  // this is a good place to test that our PrintVisitor outputs something
  // that can be parsed again to the same parse tree.
  static void RoundTripPrintParseAgainTest(bant::List *first_pass) {
    if (!first_pass) return;

    std::stringstream stringify1;
    for (Node *n : *first_pass) {
      stringify1 << n << "\n";
    }

    const std::string source1 = stringify1.str();
    NamedLineIndexedContent source("<text-reprinted>", source1);
    Scanner scanner(source);
    Arena local_arena(4096);
    Parser parser(&scanner, &local_arena, std::cerr);
    bant::List *second_pass = parser.parse();
    ASSERT_TRUE(second_pass != nullptr);

    std::stringstream stringify2;
    for (Node *n : *second_pass) {
      stringify2 << n << "\n";
    }

    EXPECT_EQ(source.content(), stringify2.str()) << " ROUNDTRIP";
  }

  Arena arena_;
};

TEST_F(ParserTest, ParseEmpty) {
  EXPECT_TRUE(Parse("")->empty());
  EXPECT_TRUE(Parse("# just a with newline\n")->empty());
  EXPECT_TRUE(Parse("# just a comment without newline")->empty());
}

// Extract the scalar on the rhs of the assignment found in list [a = 123]
static Scalar *ExtractScalar(bant::List *list) {
  Node *first_element = ABSL_DIE_IF_NULL(*list->begin());
  BinOpNode *assign = ABSL_DIE_IF_NULL(first_element->CastAsBinOp());
  Node *rhs = ABSL_DIE_IF_NULL(assign->right());
  return ABSL_DIE_IF_NULL(rhs->CastAsScalar());
}

TEST_F(ParserTest, IntConversion) {
  EXPECT_EQ(ExtractScalar(Parse("a=0o123"))->AsInt(), 0123);
  EXPECT_EQ(ExtractScalar(Parse("a=0xabc"))->AsInt(), 0xabc);
}

TEST_F(ParserTest, Assignments) {
  Node *const expected = List({
    Assign("foo", Str("regular_string", false, false)),
    Assign("backslash", Str("\\\\", false, false)),
    Assign("bar", Str("raw_string", false, true)),
    Assign("baz", Str("triple quoted", true, false)),
    Assign("quux", Str("raw triple quoted", true, true)),
  });
  EXPECT_EQ(Print(expected), Print(Parse(R"(
foo = "regular_string"
backslash = "\\"
bar = r"raw_string"
baz = """triple quoted"""
quux = R"""raw triple quoted"""
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

TEST_F(ParserTest, SimpleExpressions) {
  Node *const expected = List(
    {Assign("a", Op('+', Int(40), Int(2))),
     Assign("b", Op('/', Op('*', Int(30), Int(7)), Int(2))),
     // note: no proper precedence yet, should be like 'e'
     Assign("c", Op('/', Op('+', Int(30), Int(7)), Int(2))),
     Assign("d", Op('/', Op('+', Int(30), Int(7)), Int(2))),
     Assign("e", Op('+', Int(30), Op('/', Int(7), Int(2)))),
     Assign("f", UnaryOp(TokenType::kMinus, Int(30))),
     Assign("g", Op(TokenType::kEqualityComparison, Id("a"), Id("b"))),
     Assign("h", UnaryOp(TokenType::kNot,
                         Op(TokenType::kEqualityComparison, Id("a"), Id("b")))),
     Assign("h1", UnaryOp(TokenType::kNot, Op(TokenType::kEqualityComparison,
                                              Id("a"), Id("b")))),
     Assign("i", Op(TokenType::kNotEqual, Id("a"), Id("b"))),
     Assign("j", Number("0o123")), Assign("k", Number("0xab"))});

  EXPECT_EQ(Print(expected), Print(Parse(R"(
a = 40 + 2
b = 30 * 7 / 2
c = 30 + 7 / 2
d = (30 + 7) / 2
e = 30 + (7 / 2)
f = -30
g = a == b
h = not (a == b)
h1 = !(a == b)
i = a != b
j = 0o123  # octal number
k = 0xab   # hex number
)")));
}

TEST_F(ParserTest, InExpr) {
  Node *const expected = List({
    Assign("a", In(Str("x"), Str("foobax"))),
    Assign("b", NotIn(Str("x"), Str("foobax"))),
  });
  EXPECT_EQ(Print(expected), Print(Parse(R"(
a = "x" in "foobax"
b = "x" not in "foobax"
)")));
}

TEST_F(ParserTest, ArrayAccess) {
  Node *const expected = List({
    List({Int(1234)}),  //
    Assign("a", Op('[', Id("x"), Int(42))),
    Assign("b", Op('[', Id("x"), Op(':', Int(1), Int(2)))),
    Assign("c", Op('[', Id("x"), Op(':', Int(1), nullptr))),
    Assign("d", Op('[', Id("x"), Op(':', nullptr, Int(2)))),
    Assign("e", Op('[', Op('[', Id("x"), Int(42)), Int(17))),
    Assign("f", Id("x")),
    // List({Int(5678)}),  // see below.
    // Assign("g", Op('[', Call("h", Tuple({})), Int(42))),
  });

  EXPECT_EQ(Print(expected), Print(Parse(R"(
[1234]      # toplevel array is not an array access
a = x[42]
b = x[1:2]
c = x[1:]
d = x[:2]
e = x[42][17]
f = x       # Should not be seen as x[5678] access, as newline follows.
#[5678]     # currently wrongly parsed as array access of x in previous line.
#g = h()[42]  # currently also not parsed correctly.
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
    Assign("baz", Tuple({Str("a")})),  // Tuple with one element
    Assign("qux", Str("a")),           // Just a parenthized expression.
    Assign("buz", Tuple({Str("a")})),  // Parenthized tuple; otherwise like baz
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
  Node *const expected =
    List({Assign("str_map", Map({{Str("orange"), Str("fruit")}})),
          Assign("num_map", Map({{Str("answer"), Int(42)}})),
          Assign("id_map", Map({{Id("SOME_IDENTIFIER"), Id("ANOTHER_ID")}}))});
  EXPECT_EQ(Print(expected), Print(Parse(R"(
str_map = { "orange" : "fruit" }
num_map = { "answer" : 42 }
id_map = { SOME_IDENTIFIER : ANOTHER_ID }
)")));
}

TEST_F(ParserTest, Lists) {
  Node *const expected = List({
    Assign("empty", List({})),
    Assign("one", List({Id("a")})),
    Assign("one_c", List({Id("a")})),
    Assign("two", List({Id("a"), Id("b")})),
    Assign("two_c", List({Id("a"), Id("b")})),
    Assign("three", List({Id("a"), Id("b"), Id("c")})),
    Assign("three_c", List({Id("a"), Id("b"), Id("c")})),
  });

  EXPECT_EQ(Print(expected), Print(Parse(R"(
empty =   []
one =     [a]
one_c =   [a,]   # Same with trailling comma
two =     [a, b]
two_c =   [a, b,]
three =   [a, b, c]
three_c = [a, b, c,]
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
    Call("foo", Tuple({Str("x"), Str("y", true)})),
    List({Call("bar", Tuple({Str("a")}))}),
  });

  // Also testing fancy string literal.
  EXPECT_EQ(Print(expected), Print(Parse(R"(
foo("x", """y""")  # Triple quoted-string should look like regular one.
[bar("a")]         # function call result as a value inside a list
)")));
}

TEST_F(ParserTest, ParseListComprehension) {
  Node *const expected = List({
    // Simple expr
    ListComprehension(List::Type::kList,  //
                      For(Id("i"),        //
                          In(Tuple({Id("i")}), List({Str("x"), Str("y")})))),
    // Typical use in BUILD files is with lhs being a tuple
    ListComprehension(
      List::Type::kList,  //
      For(Tuple({Op('+', Str("foo"), Id("i"))}),
          In(Tuple({Id("i")}), List({Str("a"), Str("b"), Str("c")})))),
    // Nested for-lops
    ListComprehension(
      List::Type::kList,                                          //
      For(For(Op('+', Id("i"), Id("j")),                          //
              In(Tuple({Id("i")}), List({Str("x"), Str("y")}))),  //
          In(Tuple({Id("j")}), List({Str("a"), Str("b")})))),
    // For with two variables expanding a tuple
    ListComprehension(
      List::Type::kList,  //
      For(Op('+', Id("i"), Id("j")),
          In(Tuple({Id("i"), Id("j")}), List({Tuple({Str("a"), Str("b")}),
                                              Tuple({Str("x"), Str("y")})})))),
    // Exactly the same, but the variable list is given as tuple. Paresed the
    // same way.
    ListComprehension(
      List::Type::kList,  //
      For(Op('+', Id("i"), Id("j")),
          In(Tuple({Id("i"), Id("j")}), List({Tuple({Str("a"), Str("b")}),
                                              Tuple({Str("x"), Str("y")})})))),
    // List comprehension but for a map. Need to assign, as we don't have
    // toplevel maps.
    Assign("m", ListComprehension(
                  List::Type::kMap,                  //
                  For(Op(':', Id("i"), Str("bar")),  //
                      In(Tuple({Id("i")}), List({Str("x"), Str("y")}))))),

  });

  EXPECT_EQ(Print(expected), Print(Parse(R"(
  [i for i in [ "x", "y"]]                         # simple expr not a tuple
  [
     ("foo" + i,)  # Comma helps identify this as tuple expression
     for i in ["a", "b", "c"]
  ]
  [i + j for i in [ "x", "y"] for j in ["a", "b"]] # nested
  [i + j for i, j in [("a", "b"), ("x", "y")]]     # multi-var into tuple
  [i + j for (i, j) in [("a", "b"), ("x", "y")]]   # multi-var; var-list tuple
  m = { i : "bar" for i in ["x", "y"]}             # map tuple comprehension
)")));
}

// Make sure we don't accidentally see an opening bracket on the next line
// as array access in the previous one.
TEST_F(ParserTest, ListComprehensionAfterExpressionIsNotAnArrayAccess) {
  Node *const expected = List({
    //
    Assign("a", Op('+', Int(42), Int(8))),
    ListComprehension(List::Type::kList,  //
                      For(Id("f"),        //
                          In(Tuple({Id("f")}), List({Int(27)})))),
  });
  EXPECT_EQ(Print(expected), Print(Parse(R"(
a = 42 + 8
[ f for f in [27]]
)")));
}

TEST_F(ParserTest, ParseTernary) {
  Node *n = Parse("[foo() if a + b else baz()]");
  EXPECT_EQ(Print(n), "[[foo()\n         if a + b else baz()\n        ]]");
}
}  // namespace bant
