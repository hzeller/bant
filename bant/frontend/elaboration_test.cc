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

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/parsed-project_testutil.h"
#include "bant/session.h"
#include "gtest/gtest.h"

namespace bant {

class ElaborationTest : public testing::Test {
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

TEST_F(ElaborationTest, ConcatLists) {
  auto result = ElabAndPrint(
    R"(
FOO = ["baz.cc", "qux.cc"]
cc_library(
  name = "foo",
  srcs = [ "foo.cc" ] + [ "bar.cc" ] + FOO
)
)",
    R"(
FOO = ["baz.cc", "qux.cc"]
cc_library(
  name = "foo",
  srcs = [ "foo.cc", "bar.cc", "baz.cc", "qux.cc" ],
)
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, ConcatListWithUndefinedValue) {
  auto result = ElabAndPrint(
    R"(
# UNDEFINED_VALUE
cc_library(
  name = "foo",
  srcs = [ "foo.cc" ] + UNDEFINED + [ "bar.cc" ],
)
)",
    R"(
cc_library(
  name = "foo",
  srcs = [ "foo.cc", "bar.cc" ]    # best effort result
)
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, ListComprehension) {
  auto result = ElabAndPrint(
    R"lc-in(
A = [ "num={}".format(i) for i in [1, 2, 3] ]
B = [ "pair=({}, {})".format(i, j) for (i,j) in [(1,2), (10,20), (23,42)] ]

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
BAZ = (-9) + 7  # Since we don't do precedence yet, need to paren around value
)",
    R"(
FOO = 13
BAR = -4
BAZ = -2
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, ConcatStrings) {
  auto result = ElabAndPrint(
    R"(
BAZ = "baz"
cc_library(
  name = "foo" + "bar" + BAZ,
  include_prefix = ("foo" + "bar") + "qux",
)
)",
    R"(
BAZ = "baz"
cc_library(
  name = "foobarbaz",
  include_prefix = "foobarqux",
)
)");

  EXPECT_EQ(result.first, result.second);

  // Let's see that the 'location' of the assembled string points to the
  // original '+' operand.
  //
  // Unlike one would expect intuitively, the way the parser currently handles
  // '+' is right-associative, so evaluation happens from right to left:
  // "foo" + ("bar" + BAZ)).
  // Since string-concat is assoicative that is a perfectly fine.
  // But this is why the first, not last, '+' is shown as file location.
  query::FindTargets(elaborated()->ast, {}, [&](const query::Result &result) {
    EXPECT_EQ(result.name, "foobarbaz");
    EXPECT_EQ(project().Loc(result.name), "//elab/BUILD:4:16:");

    // Parenthesis around second expression: Second plus is 'location'
    EXPECT_EQ(result.include_prefix, "foobarqux");
    EXPECT_EQ(project().Loc(result.include_prefix), "//elab/BUILD:5:36:");
  });
}

TEST_F(ElaborationTest, PercentFormat) {
  auto result = ElabAndPrint(
    R"(
FOO = "Hello %s" % ("World",)
BAR = "%s is %s." % ("Answer", 42)
)",
    R"(
FOO = "Hello World"
BAR = "Answer is 42."
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
)",
    R"(
FOO = "Hello World"
BAR = "Answer is 42."
SHORT_FMT = "Just Parameters no more fmt."
SHORT_ARGS = "Some text and 123 and {}"
NOT_ALL_CONST = "{} and {}".format("text", not_a_constant)
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
B = "some-filename.foo.bar.txt".rsplit(".", 1)
C = "some-filename".rsplit(".", 1)
D = ("remove-suffix.txt".rsplit(".", 1))[0]
)",
    R"(
S = ["some", "space", "separated"]
A = ["some-filename", "foo", "bar", "txt"]
B = ["some-filename.foo.bar", "txt"]
C = ["some-filename"]
D = "remove-suffix"
)");

  EXPECT_EQ(result.first, result.second);
}

TEST_F(ElaborationTest, ArrayAccess) {
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

namespace fs = std::filesystem;

// TODO: lift out if useful in other tests.
class ChangeToTmpDir {
 public:
  explicit ChangeToTmpDir(std::string_view base)
      : dir_before_(fs::current_path(error_receiver_)) {
    auto dir = fs::path(::testing::TempDir()) / base;
    CHECK(fs::create_directory(dir, error_receiver_));
    fs::current_path(dir, error_receiver_);
  }

  ~ChangeToTmpDir() { fs::current_path(dir_before_, error_receiver_); }

  void touch(std::string_view relative_to, std::string_view file) {
    fs::path path;
    if (!relative_to.empty()) {
      path = fs::path(relative_to) / file;
    } else {
      path = file;
    }
    if (path.has_parent_path()) {
      fs::create_directories(path.parent_path(), error_receiver_);
    }
    const std::ofstream touch(path);  // destructor will flush file.
  }

 private:
  std::error_code error_receiver_;
  fs::path dir_before_;
};

static std::pair<std::string, std::string> TestGlobFile(
  std::string_view test_name, ElaborationTest *test, std::string_view package,
  std::string_view filename, std::string_view glob_pattern) {
  ChangeToTmpDir tmpdir(test_name);

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
