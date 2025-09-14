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

#include "bant/frontend/elaboration.h"

#include <iostream>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/str_format.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/parsed-project_testutil.h"
#include "bant/session.h"
#include "bant/util/file-test-util.h"
#include "gtest/gtest.h"

namespace bant {

class ElaborationTest : public ::testing::Test {
 public:
  // Put "to_elaborate" into "package" and elaborate.
  // Also parse "expected" into package //expected and return printout of
  // each package.
  std::pair<std::string, std::string> ElabInPackageAndPrint(
    std::string_view package, std::string_view to_elaborate,
    std::string_view expected,
    const CommandlineFlags &flags = CommandlineFlags{
      .verbose = 1,
      .builtin_macro_expand = true,
    }) {
    elaborated_ = pp_.Add(package, to_elaborate);

    const ParsedBuildFile *expected_parsed = pp_.Add("//expected", expected);

    EXPECT_EQ(pp_.project().error_count(), 0) << "invalid test inputs.";

    const ElaborationOptions elab_options{.builtin_macro_expansion =
                                            flags.builtin_macro_expand};
    Session session(&std::cerr, &std::cerr, flags);
    const std::string elab_print =
      ToString(bant::Elaborate(session, &pp_.project(), elaborated_->package,
                               elab_options, elaborated_->ast));

    const std::string expect_print = ToString(expected_parsed->ast);
    return {elab_print, expect_print};
  }

  // Like ElabInPackageAndPrint(), but default package to //elab.
  std::pair<std::string, std::string> ElabAndPrint(
    std::string_view to_elaborate, std::string_view expected,
    const CommandlineFlags &flags = CommandlineFlags{
      .verbose = 1,
      .builtin_macro_expand = true,
    }) {
    return ElabInPackageAndPrint("//elab", to_elaborate, expected, flags);
  }

  // Returns the last elaborated file.
  const ParsedBuildFile *elaborated() { return elaborated_; }

  ParsedProject &project() { return pp_.project(); }

 private:
  ParsedProjectTestUtil pp_;
  const ParsedBuildFile *elaborated_ = nullptr;
};

TEST_F(ElaborationTest, ExpandVariables) {
  auto result = ElabAndPrint(
    R"(
BAR = "bar.cc"
BAR_REF = BAR        # let's create a couple of indirections
SOURCES = ["foo.cc", BAR_REF]

cc_library(
  name = "foo",
  srcs = SOURCES,    # global variable SOURCES should be expanded
  baz = name,        # nested symbol 'name' should not be expanded
)
)",
    R"(
BAR = "bar.cc"
BAR_REF = "bar.cc"   # ... indirections resolved
SOURCES = ["foo.cc", "bar.cc"]

cc_library(
  name = "foo",
  srcs = ["foo.cc", "bar.cc"],  # <- expanded
  baz = name,                   # <- not expanded
)
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, OnlyIdentifiersOnRhsAreExpanded) {
  auto result = ElabAndPrint(
    R"(
name = "hello"
cc_library(
   name = name    # same name, but lhs an rhs need to be treated differently
)
)",
    R"(
name = "hello"
cc_library(
  name = "hello"  # ... and they are
)
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, UnpackIntoTuple) {
  auto result = ElabAndPrint(
    R"(
(a, b) = (42, 123)
(12, c) = (1, 5)       # Semantic nonsense; should make the best out of it
(x, y, z) = (a, b, c)  # Testing one more indirection; this time lhs list
d = x + y + z
)",
    R"(
(a, b) = (42, 123)
(12, c) = (1, 5)
(x, y, z) = (42, 123, 5)
d = 170
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, UnpackIntoToplevelListIsTreatedAsAssignmentToTuple) {
  // Essentially the same as tuple
  auto result = ElabAndPrint(
    R"(
a, b = (42, 123)
x, y = (a, b)
d = x + y
)",
    R"(
(a, b) = (42, 123)
(x, y) = (42, 123)
d = 165
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, ConcatLists) {
  auto result = ElabAndPrint(
    R"(
FOO = ["baz.cc", "qux.cc"]
BAR = [ "foo.cc" ] + [ "bar.cc" ] + FOO
LEFT_EMPTY = [] + ["a", "b"]
RIGHT_EMPTY = ["a", "b"] + []
)",
    R"(
FOO = ["baz.cc", "qux.cc"]
BAR = [ "foo.cc", "bar.cc", "baz.cc", "qux.cc" ]
LEFT_EMPTY = ["a", "b"]
RIGHT_EMPTY = ["a", "b"]
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, ConcatListWithUndefinedValue) {
  auto result = ElabAndPrint(
    R"(
# UNDEFINED_VALUE
FOO = [ "foo.cc" ] + UNDEFINED + [ "bar.cc" ]
)",
    R"(
FOO = [ "foo.cc", "bar.cc" ]    # best effort result
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, ListComprehension) {
  auto result = ElabAndPrint(
    R"lc-in(
A = [ "num={}".format(i) for i in [1, 2, 3] ]
B = [ "pair=({}, {})".format(i, j) for (i,j) in [(1,2), (10,20), (23,42)] ]
M = { foo : bar for (foo,bar) in [("x", 1), ("y", 2), ("z", 3)] }

IN_LIST = ["a", "b" ]
C = [ "{}.h".format(file) for file in IN_LIST ]  # IN_LIST: expand first

D = [ ">{}, {}, {}<".format(i, j, k)
      for i in [1, 2]
      for j in [7, 8]
      for k in ["a", "b"]
    ]
)lc-in",
    R"lc-result(
A = [ "num=1", "num=2", "num=3" ]
B = [ "pair=(1, 2)", "pair=(10, 20)", "pair=(23, 42)"]
M = { "x" : 1, "y" : 2, "z" : 3 }

IN_LIST = ["a", "b" ]
C = [ "a.h", "b.h" ]

# Note: this is not a correct starlark-equivalent result: the order of
# evaluation should be outside-in, and the result should be a single list,
# not a nested one.
# It will work for toplevel rules, but not
D = [
     [
       [">1, 7, a<", ">2, 7, a<"],
       [">1, 8, a<", ">2, 8, a<"],
     ],
     [
       [">1, 7, b<", ">2, 7, b<"],
       [">1, 8, b<", ">2, 8, b<"],
     ],
]
)lc-result");
  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, SelectChoosesConditionDefault) {
  auto result = ElabAndPrint(
    R"(
cc_library(
  name = "foo",
  srcs = select({
     "//:foo"               : ["abc.cc"],
     [ "not-a-string"]      : ["baz.cc"],
     "//conditions:default" : ["def.cc"],
   })
)
)",
    R"(
cc_library(
  name = "foo",
  srcs = ["def.cc"]   # No condition set, choosing default
)
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, SelectWithChosenOption) {
  CommandlineFlags flags = CommandlineFlags{.verbose = 1};
  flags.custom_flags.emplace("//:foo");
  auto result = ElabAndPrint(
    R"(
cc_library(
  name = "foo",
  srcs = select({
     "//:foo"               : ["abc.cc"],
     [ "not-a-string"]      : ["baz.cc"],
     "//conditions:default" : ["def.cc"],
   })
)
)",
    R"(
cc_library(
  name = "foo",
  srcs = ["abc.cc"]
)
)",
    flags);

  EXPECT_EQ(result.first, result.second);
}

// Note: arithmetic is really basic, as we don't implement precedence yet
TEST_F(ElaborationTest, BasicArith) {
  auto result = ElabAndPrint(
    R"(
FOO = 1 + 3 + 9
BAR = 3 - 7
BAQ = 3 - -7
BAZ = -9 + 7
QIX = 9 * 9 + 1  # simple precedence
QUX = 1 + 9 * 9  # .. test
FIX = 9 * (9 + 1)
FUX = (1 + 9) * 9
)",
    R"(
FOO = 13
BAR = -4
BAQ = 10
BAZ = -2
QIX = 82
QUX = 82
FIX = 90
FUX = 90
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, ConcatStrings) {
  auto result = ElabAndPrint(
    R"(
BAZ = "baz"
cc_library(
  name = "foo" + "bar" + BAZ,
#                      ^-- column 24 (second plus assembles the final string)
  include_prefix = "foo" + ("bar" + "qux"),
#                        ^-- column 26 (evaluated after "barqux" assembled)
)
LEFT_EMPTY = "" + "a"
RIGHT_EMPTY = "b" + ""
)",
    R"(
BAZ = "baz"
cc_library(
  name = "foobarbaz",
  include_prefix = "foobarqux",
)
LEFT_EMPTY = "a"
RIGHT_EMPTY = "b"
)");

  EXPECT_EQ(result.first, result.second);

  // Let's see that the 'location' of the assembled string points to the
  // original '+' operand.
  query::FindTargets(elaborated()->ast, {}, [&](const query::Result &result) {
    EXPECT_EQ(result.name, "foobarbaz");
    EXPECT_EQ(project().Loc(result.name), "//elab/BUILD:4:24:");

    // Parenthesis around right sub-expression: First plus is 'location'
    EXPECT_EQ(result.include_prefix, "foobarqux");
    EXPECT_EQ(project().Loc(result.include_prefix), "//elab/BUILD:6:26:");
  });
}

TEST_F(ElaborationTest, PercentFormat) {
  auto result = ElabAndPrint(
    R"(
FOO = "Hello %s" % "World"
BAR = "Hello %s..." % ("World",)
BAZ = "%s is %s." % ("Answer", 42)
)",
    R"(
FOO = "Hello World"
BAR = "Hello World..."
BAZ = "Answer is 42."
)");

  EXPECT_EQ(result.first, result.second);
}
TEST_F(ElaborationTest, FormatStringPositional) {
  auto result = ElabAndPrint(
    R"(
FOO = "Hello {}".format("World")
BAR = "{} is {}.".format("Answer", 42)
SHORT_FMT = "Just {} no more fmt.".format("Parameters", "are", "too", "many")
SHORT_ARGS = "Some {} and {} and {}".format("text", 123)
NOT_ALL_CONST = "{} and {}".format("text", not_a_constant)
HIGH_PRECEDENCE_DOT = "foo_" + "here {}".format("bar") + "_baz"
)",
    R"(
FOO = "Hello World"
BAR = "Answer is 42."
SHORT_FMT = "Just Parameters no more fmt."
SHORT_ARGS = "Some text and 123 and {}"
NOT_ALL_CONST = "{} and {}".format("text", not_a_constant)
HIGH_PRECEDENCE_DOT = "foo_here bar_baz"
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, FormatStringSelectArg) {
  auto result = ElabAndPrint(
    R"(
FOO = "Hello {0}".format("World")
BAR = "{1} is {0}.".format("Answer", 42)
SHORT_FMT = "Just {3} no more fmt.".format("Parameters", "are", "too", "many")
SHORT_ARGS = "Some {} and {1} and {0} and {}".format("text", 123)
INVALID_ARGS = "Some {77}".format("text")
)",
    R"(
FOO = "Hello World"
BAR = "42 is Answer."
SHORT_FMT = "Just many no more fmt."
SHORT_ARGS = "Some text and 123 and text and {}"
INVALID_ARGS = "Some "
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, FormatStringKwArg) {
  auto result = ElabAndPrint(
    R"(
FOO = "Hello {address}".format(address = "World")
BAR = "{text} is {number}.".format( text = "Answer", number = 42)
MIXED = "{text} and {1}".format( text = "hello", "world" )
MIXED_1 = "{0} and {1}".format( text = "hello", "world" )
)",
    R"(
FOO = "Hello World"
BAR = "Answer is 42."
MIXED = "hello and world"
MIXED_1 = "hello and world"
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, JoinStrings) {
  auto result = ElabAndPrint(
    R"(
FOO = "😊".join(["Hello", "universe", 42, "is" + " answer"])
BAR = ",".join()                           # invalid non-parameter
BAZ = ",".join(["Hello", not_a_constant])  # parameter not fully const-eval'ed
QUX = " is ".join(("tuple", "ok"))         # besides arrays, tuples are also ok
)",
    R"(
FOO = "Hello😊universe😊42😊is answer"
BAR = ",".join()  # left as is
BAZ = ",".join(["Hello", not_a_constant])  # left as is
QUX = "tuple is ok"
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, RsplitStrings) {
  auto result = ElabAndPrint(
    R"(
S = "some space separated".rsplit()
A = "some-filename.foo.bar.txt".rsplit(".")
A1 = "some-filename.foo.bar.txt".rsplit(".", -1)  # equivalent to split all
B = "some-filename.foo.bar.txt".rsplit(".", 1)
C = "some-filename".rsplit(".", 1)
D = ("remove-suffix.txt".rsplit(".", 1))[0]  # TODO: should work without parens?
E = "Hello the fillword the remove".rsplit(" the ")
)",
    R"(
S = ["some", "space", "separated"]
A = ["some-filename", "foo", "bar", "txt"]
A1 = ["some-filename", "foo", "bar", "txt"]
B = ["some-filename.foo.bar", "txt"]
C = ["some-filename"]
D = "remove-suffix"
E = ["Hello", "fillword", "remove"]
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, SplitStrings) {
  auto result = ElabAndPrint(
    R"(
S = "some space separated".split()
A = "some-filename.foo.bar.txt".split(".")
A1 = "some-filename.foo.bar.txt".split(".", -1)
A2 = "some-filename.foo.bar.txt".split(".")[1]
B = "some-filename.foo.bar.txt".split(".", 1)
C = "some-filename".split(".", 1)
D = ("get-prefix.tar.gz".split("."))[0]  # TODO: should work without parens ?
E = "Hello the fillword the remove".split(" the ")
)",
    R"(
S = ["some", "space", "separated"]
A = ["some-filename", "foo", "bar", "txt"]
A1 = ["some-filename", "foo", "bar", "txt"]
#A2 = "foo"                                     # did not evaluate...
A2 = "some-filename.foo.bar.txt".split(".")[1]  # ... because wrong precedence
B = ["some-filename", "foo.bar.txt"]
C = ["some-filename"]
D = "get-prefix"
E = ["Hello", "fillword", "remove"]
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, StringInList) {
  auto result = ElabAndPrint(
    R"(
FOO = "foo" in [ "bar", "foo", "baz" ]
FOO = "foo" not in [ "bar", "foo", "baz" ]
FOO = "foo" not in [ "bar", "qux", "baz" ]
NOT_UNKNOWN = "foo" in [ variable, "foo" ]  # has variable, but contained
UNKNOWN =     "foo" in [ variable, "bar" ]  # has variable, so unknown
)",
    R"(
FOO = True
FOO = False
FOO = True
NOT_UNKNOWN = True
UNKNOWN = "foo" in [ variable, "bar" ]  # keep expression as-is
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, StringInString) {
  auto result = ElabAndPrint(
    R"(
FOO = "bar" in "foobarbaz"
FOO = "bar" in "fooquxbaz"
FOO = "bar" not in "fooquxbaz"
)",
    R"(
FOO = True
FOO = False
FOO = True
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, LenFunction) {
  auto result = ElabAndPrint(
    R"(
FOO = len("hello")
BAR = len(variable)
BAZ = len(["a", "b", "c"])
EXAMPLE = "somefilename.txt"[:0-len(".txt")]  # TODO not unary: precedence issue
)",
    R"(
FOO = 5
BAR = len(variable)
BAZ = 3
EXAMPLE = "somefilename"
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, Ternary) {
  auto result = ElabAndPrint(
    R"(
POS = "foo" if True else "bar"
NEG = "foo" if False else "bar"
FOO = "foo" if "e" in "yes" else "bar"
SMALL_TESTS=["f" + "oo", "bar", "baz"]  # make sure it is evaluated
TAG = ["small"] if "foo" in SMALL_TESTS else ["moderate"]
TAG = ["small"] if "foo" not in SMALL_TESTS else ["mod" + "erate"]
UNDEFINED = "foo" if variable else "bar"
)",
    R"(
POS = "foo"
NEG = "bar"
FOO = "foo"
SMALL_TESTS=["foo", "bar", "baz"]
TAG = ["small"]
TAG = ["moderate"]
UNDEFINED = "foo" if variable else "bar"
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, StringIndexAccess) {
  auto result = ElabAndPrint(
    R"(
FOO = "hello"[0]
BAR1 = "hello"[-1]
BAR2 = "hello"[4]
BAZ1 = "hello"[-10]  # graceful out of bounds handling
BAZ2 = "hello"[10]
  )",
    R"(
FOO = "h"
BAR1 = "o"
BAR2 = "o"
BAZ1 = ""
BAZ2 = ""
)");
  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, StringSliceAccess) {
  auto result = ElabAndPrint(
    R"(
FOO = "hello"[0]
FOO = "hello"[0:1]
FOO = "hello"[0:2]
FOO = "hello"[7 + -7:1+1]
BAR = "hello"[-2:1]
BAZ = "hello"[-2:-1]
QUX = "hello"[-40:-2]
ALL = "hello"[-40:40]
EXAMPLE = "file.txt"[:-4]
  )",
    R"(
FOO = "h"
FOO = "h"
FOO = "he"
FOO = "he"
BAR = ""
BAZ = "l"
QUX = "hel"
ALL = "hello"
EXAMPLE = "file"
)");
  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, ArrayIndexAccess) {
  auto result = ElabAndPrint(
    R"(
FOO_0       = ["a", "b", "c"][0]
FOO_2       = ["a", "b", "c"][2]
NOF_OO      = ["a", "b", "c"][42]

BAR_BACK    = ["a", "b", "c"][ 0 - 1]  # bin-op
BAR_BACK_1  = ["a", "b", "c"][-1]  # unary minus
BAR_BEGIN   = ["a", "b", "c"][-3]
NO_BAR      = ["a", "b", "c"][-42]

MULTI_DIM   = [("a", "b"), ("c", "d")][1][0]
MULTI_DIM2   = [("a", "b"), ("c", "d")][-2][1]
)",
    R"(
FOO_0       = "a"
FOO_2       = "c"
NOF_OO      = ["a", "b", "c"][42]

BAR_BACK    = "c"
BAR_BACK_1  = "c"
BAR_BEGIN   = "a"
NO_BAR      = ["a", "b", "c"][-42]

MULTI_DIM   = "c"
MULTI_DIM2  = "b"
)");
  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, ArraySliceAccess) {
  auto result = ElabAndPrint(
    R"(
MYLIST = ["a", "b", "c"]
FOO_0  = MYLIST[0:1]
FOO_1  = MYLIST[:1]
FOO_2  = MYLIST[1:2]
FOO_2  = MYLIST[1:3]
FOO_3  = MYLIST[1:30]  # graceful clipping
BAR_0  = MYLIST[-1:]
BAR_1  = MYLIST[2:2]
BAR_2  = MYLIST[-1:-1]
BAR_3  = MYLIST[-3:-1]
BAR_4  = MYLIST[-7:-1]  # graceful clipping
)",
    R"(
MYLIST = ["a", "b", "c"]
FOO_0  = ["a"]
FOO_1  = ["a"]
FOO_2  = ["b"]
FOO_2  = ["b", "c"]
FOO_3  = ["b", "c"]
BAR_0  = ["c"]
BAR_1  = []
BAR_2  = []
BAR_3  = ["a", "b"]
BAR_4  = ["a", "b"]
)");
  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, MapAccess) {
  auto result = ElabAndPrint(
    R"(
# Map with different types as keys
FOO = { 'hello' : 'hi', 'answer' : '42', 1024 : 'kibi' }
BAR = FOO['hello']
KIB = FOO[1024]
KIB2 = FOO[512 * 2]
BAZ = FOO['no-such-key']
QUX = FOO[1]
GET_FOUND = FOO.get('hello', 'no-used')
GET_FALLBACK = FOO.get(1, 42)
GET_FALLBACKLIST = FOO.get(1, ['some', 'list'])
  )",
    R"(
FOO = { 'hello' : 'hi', 'answer' : '42', 1024 : 'kibi' }
BAR = "hi"
KIB = "kibi"
KIB2 = "kibi"
# Keys not found: Don't fail but keep expression as-is
BAZ = { 'hello' : 'hi', 'answer' : '42', 1024 : 'kibi' }['no-such-key']
QUX = { 'hello' : 'hi', 'answer' : '42', 1024 : 'kibi' }[1]
GET_FOUND = 'hi'
GET_FALLBACK = 42
GET_FALLBACKLIST = ['some', 'list']
)");
  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, MapKeys) {
  auto result = ElabAndPrint(
    R"(
FOO = { 'hello' : 'hi', 'answer' : '42', 1024 : 'kibi' }
BAR = FOO.keys()
BARITEMS = FOO.items()

BAZ = { 'x' : 1, 'y' : 2, 'z' : 3}.keys()  # call directly on literal
QUX = [element for element in { 'x' : 1, 'y' : 2, 'z' : 3}.keys()]
  )",
    R"(
FOO = { 'hello' : 'hi', 'answer' : '42', 1024 : 'kibi' }
BAR = [ 'hello', 'answer', 1024 ]
BARITEMS = [ ('hello', 'hi'), ('answer', '42'), (1024, 'kibi') ]

BAZ = [ 'x', 'y', 'z' ]
QUX = [ 'x', 'y', 'z' ]
)");
  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, MapValues) {
  auto result = ElabAndPrint(
    R"(
FOO = { 'hello' : 'hi', 'answer' : '42', 1024 : 'kibi' }
BAR = FOO.values()
BAZ = { 'x' : 1, 'y' : 2, 'z' : 3}.values()
QUX = [element for element in { 'x' : 1, 'y' : 2, 'z' : 3}.values()]
  )",
    R"(
FOO = { 'hello' : 'hi', 'answer' : '42', 1024 : 'kibi' }
BAR = [ 'hi', '42', 'kibi' ]
BAZ = [ 1, 2, 3 ]
QUX = [ 1, 2, 3 ]
)");
  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, MapMerge) {
  auto result = ElabAndPrint(
    R"(
FOO = {'something' : 'foo'} | {'another' : 'bar'}
FOO = {'to_replace' : 'foo'} | {'to_replace' : 'bar'}
FOO = ({'keep' : 1, 'original': 2, 'key' : 3, 'order': 4} |
       {'order': 8, 'key' : 7, 'additional': 9, 'keep' : 5, 'original' : 6})
BAR = { not_a_constexpr : 'foo'} | {'another' : 'bar'}

const_evaluated = "hello"
BAZ = { const_evaluated : 'foo'} | {'another' : 'bar'}
)",
    R"(
FOO = {'something': 'foo', 'another': 'bar'}
FOO = {'to_replace': 'bar'}
FOO = {'keep': 5, 'original': 6, 'key': 7, 'order': 8, 'additional': 9}
BAR = { not_a_constexpr : 'foo'} | {'another' : 'bar'}

const_evaluated = "hello"
BAZ = {'hello': 'foo', 'another': 'bar'}
)");

  EXPECT_EQ(result.first, result.second);
}

static std::pair<std::string, std::string> TestGlobFile(
  std::string_view test_name, ElaborationTest *test, std::string_view package,
  std::string_view filename, std::string_view glob_pattern) {
  bant::test::ChangeToTmpDir tmpdir(test_name);

  // Creating the file relative to the package path, as we glob relative to it.
  tmpdir.touch(package, filename);

  return test->ElabInPackageAndPrint(
    package.empty() ? "//" : package,
    absl::StrFormat(R"(foo = glob(include = ["%s"]))", glob_pattern),
    absl::StrFormat(R"(foo = ["%s"])", filename));
}

TEST_F(ElaborationTest, GlobInToplevel) {
  auto result = TestGlobFile("GlobInToplevel", this, "",  //
                             "foo.txt", "*.txt");
  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, GlobInSubpackage) {
  auto result = TestGlobFile("GlobInSubpackage", this, "some/pkg",  //
                             "foo.txt", "*.txt");
  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, GlobDirInToplevel) {
  auto result = TestGlobFile("GlobDirInToplevel", this, "",  //
                             "abc/foo.xyz", "**/*.xyz");
  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, GlobDirKnownPrefixInToplevel) {
  // Test common prefix optimization - search only needs to start in abc/
  auto result = TestGlobFile("GlobDirKnownPrefixInToplevel", this, "",  //
                             "abc/foo.xyz", "abc/*.xyz");
  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, GlobDirInSubpackage) {
  auto result = TestGlobFile("GlobDirInSubpackage", this, "some/pkg",  //
                             "abc/foo.xyz", "**/*.xyz");
  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, GlobDirKnownPrefixInSubpackage) {
  // Test common prefix optimization - search only needs to start in abc/
  auto result =
    TestGlobFile("GlobDirKnownPrefixInSubpackage", this, "some/pkg",  //
                 "abc/foo.xyz", "abc/*.xyz");
  EXPECT_EQ(result.first, result.second);
}

}  // namespace bant
