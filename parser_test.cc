#include "parser.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <sstream>
#include <string_view>
#include <utility>

#include "arena.h"
#include "ast.h"

class ParserTest : public testing::Test {
 protected:
  ParserTest() : arena_(4096) {}

  ::List *Parse(std::string_view text) {
    Scanner scanner(text);
    Parser parser(&scanner, &arena_, "<text>", std::cerr);
    return parser.parse();
  }

  // Some helpers to build ASTs to compre
  StringScalar *Str(std::string_view s) { return arena_.New<StringScalar>(s); }
  Identifier *Id(std::string_view i) { return arena_.New<Identifier>(i); }
  BinOpNode *Op(char op, Node *a, Node *b) {
    return arena_.New<BinOpNode>(a, b, op);
  }
  Assignment *Assign(std::string_view id, Node *b) {
    return arena_.New<Assignment>(Id(id), b);
  }
  FunCall *Call(std::string_view id, ::List *args) {
    return arena_.New<::FunCall>(Id(id), args);
  }

  ::List *List(std::initializer_list<Node *> elements) {
    ::List *result = arena_.New<::List>(List::Type::kList);
    for (Node *n : elements) result->Append(&arena_, n);
    return result;
  }

  ::List *Tuple(std::initializer_list<Node *> elements) {
    ::List *result = arena_.New<::List>(List::Type::kTuple);
    for (Node *n : elements) result->Append(&arena_, n);
    return result;
  }
  ::List *Map(
    std::initializer_list<std::pair<std::string_view, Node *>> elements) {
    ::List *result = arena_.New<::List>(List::Type::kMap);
    for (const auto &n : elements) {
      result->Append(&arena_, Op(':', Str(n.first), n.second));
    }
    return result;
  }

  ::ListComprehension *ListComprehension(::List *pattern, ::List *vars,
                                         ::Node *source) {
    return arena_.New<::ListComprehension>(pattern, vars, source);
  }

  std::string Print(Node *n) {
    std::stringstream s;
    s << n;
    return s.str();
  }

 private:
  Arena arena_;
};

TEST_F(ParserTest, FunctionCalls) {
  Node *const expected = List({
    Call("foo", Tuple({Str("foo"), Id("k")})),
    Op('.', Id("nested"), Call("bar", Tuple({Str("baz"), Id("m")}))),
  });

  EXPECT_EQ(Print(expected), Print(Parse(R"(
foo("foo", k)
nested.bar("baz", m)
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
    Tuple({Op('+', Str("foo"), Id("i"))}), List({Id("i")}),  //
    List({Str("a"), Str("b"), Str("c")}))});

  EXPECT_EQ(Print(expected), Print(Parse(R"(
  [
     ("foo" + i,)
     for i in ["a", "b", "c"]
  ]
)")));
}
