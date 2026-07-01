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

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/parsed-project_testutil.h"
#include "bant/session.h"
#include "bant/tool/dwyu-internal.h"
#include "bant/tool/edit-callback_testutil.h"
#include "bant/types-bazel.h"
#include "bant/util/stat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::HasSubstr;

namespace bant {
namespace {

// A DWYUGenerator with a mocked-out way to extract the sources.
class TestableDWYUGenerator : public bant::DWYUGenerator {
 public:
  using DWYUGenerator::DWYUGenerator;

  void AddSource(std::string_view name, std::string_view content) {
    source_content_[name] = content;
  }

 protected:
  std::optional<SourceFile> TryOpenFile(std::string_view source_file,
                                        Stat &) override {
    auto found = source_content_.find(source_file);
    if (found == source_content_.end()) return std::nullopt;
    return SourceFile{
      .content = found->second, .path = found->first, .is_generated = false};
  }

 private:
  absl::flat_hash_map<std::string, std::string> source_content_;
};

// Putthing it all together
class DWYUTestFixture {
 public:
  DWYUTestFixture(const ParsedProject &project, const CommandlineFlags &flags)
      : session_{&log_messages_, &log_messages_, &log_messages_,
                 MakeAtLeastVerbosity(flags, 1)},
        dwyu_(session_, project, edit_expector_.checker()) {}

  ~DWYUTestFixture() {
    // Make sure that if there is a log output that the test will look for it.
    EXPECT_TRUE(log_content_requested_ || log_messages_.str().empty())
      << "Encountered messages, but never requested output to check\n"
      << log_messages_.str();
  }

  EditExpector &ExpectAdd(std::string_view target) {
    return edit_expector_.ExpectAdd(target);
  }

  EditExpector &ExpectRemove(std::string_view target) {
    return edit_expector_.ExpectRemove(target);
  }

  void AddSource(std::string_view name, std::string_view content) {
    dwyu_.AddSource(name, content);
  }

  void RunForTarget(std::string_view target) {
    auto pattern_or = BazelPattern::ParseFrom(target);
    CHECK(pattern_or.has_value());
    EXPECT_EQ(dwyu_.CreateEditsForPattern(*pattern_or), 1)
      << "Expected exactly one pattern matching. Typo calling RunForTarget() ?";
  }

  std::string LogContent() {
    log_content_requested_ = true;
    return log_messages_.str();
  }

 private:
  static CommandlineFlags MakeAtLeastVerbosity(CommandlineFlags flags,
                                               int verbosity) {
    flags.verbose = std::max(flags.verbose, 1);
    return flags;
  }

  std::stringstream log_messages_;
  Session session_;
  EditExpector edit_expector_;
  TestableDWYUGenerator dwyu_;
  bool log_content_requested_{false};
};

TEST(DWYUTest, Add_MissingDependency) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"]
)

cc_library(
  name = "bar",
  hdrs = ["bar.h"],   # make sure to not self-add :bar
  srcs = ["bar.cc"],
  # needed :foo dependency not given
)

cc_library(
  name = "baz",
  hdrs = ["src/baz.h", "src/bar.h"],
  srcs = ["src/baz.cc"],  # includes bar.h, which refers to our src/bar.h
  # needed :foo, but not bar
)
)");

  {
    DWYUTestFixture tester(pp.project(), {.verbose = 3});
    tester.ExpectAdd(":foo");
    tester.AddSource("some/path/bar.h", "");
    tester.AddSource("some/path/bar.cc", R"(
#include "some/path/foo.h"
#include "some/path/bar.h"
)");
    tester.RunForTarget("//some/path:bar");
    // listing target without checkmark
    EXPECT_THAT(tester.LogContent(), HasSubstr("- //some/path:foo"));
  }

  {  // Files relative to current directory are properly handled.
    DWYUTestFixture tester(pp.project(), {.verbose = 2});
    tester.ExpectAdd(":foo");
    tester.AddSource("some/path/bar.h", "");
    tester.AddSource("some/path/bar.cc", R"(
#include "foo.h"
)");
    tester.RunForTarget("//some/path:bar");
    EXPECT_THAT(tester.LogContent(), HasSubstr("Consider FQN"));
  }

  {  // Files relative to current directory are properly handled.
    DWYUTestFixture tester(pp.project(), {.verbose = 2});
    tester.ExpectAdd(":foo");
    // Should not be added: :bar, as the bar header is meant to come from local
    tester.AddSource("some/path/src/baz.h", "");
    tester.AddSource("some/path/src/bar.h", "");
    tester.AddSource("some/path/src/baz.cc", R"(
#include "some/path/src/baz.h" // local baz, properly
#include "bar.h"   // relative bar.h from this library not the other one
#include "foo.h"   // this requires :foo
)");
    tester.RunForTarget("//some/path:baz");
    EXPECT_THAT(tester.LogContent(), HasSubstr("Consider FQN"));
  }

  // Fuzzy matching. We match files from the suffix so as a fallback
  // we allow for matching that.
  {  // Files that match full path but are longer are guessed to belong
    DWYUTestFixture tester(pp.project(), {.verbose = 2});
    tester.ExpectAdd(":foo");  // fuzzyily can match header file, add library.
    tester.AddSource("some/path/bar.h", "");
    tester.AddSource("some/path/bar.cc", R"(
#include "external/project/some/path/foo.h"
)");
    tester.RunForTarget("//some/path:bar");
    EXPECT_THAT(tester.LogContent(), HasSubstr("provides shorter same-suffix"));
  }

  {  // Files that are somewhat shorter are also matched.
    DWYUTestFixture tester(pp.project(), {.verbose = 2});
    tester.ExpectAdd(":foo");  // fuzzyily can match header file, add library.
    tester.AddSource("some/path/bar.h", "");
    tester.AddSource("some/path/bar.cc", R"(
#include "path/foo.h"
)");
    tester.RunForTarget("//some/path:bar");
    EXPECT_THAT(tester.LogContent(), HasSubstr("provides longer same-suffix"));
  }

  {  // .. but not too short. Here, only the last path element matches.
    DWYUTestFixture tester(pp.project(), {});
    // no add expected: can't successfully match header file.
    tester.AddSource("some/path/bar.h", "");
    tester.AddSource("some/path/bar.cc", R"(
#include "wrongpath/foo.h"
)");
    tester.RunForTarget("//some/path:bar");
    EXPECT_THAT(tester.LogContent(), HasSubstr("(unknown provider)"));
    EXPECT_THAT(tester.LogContent(), HasSubstr("Missing or from non-standard"));
  }
}

TEST(DWYUTest, DependencyCheckIncludesAgainstPathFromIncludesAttribute) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "foo",
  srcs = [
     "src/subdir/foo.cc",
     "src/foo.h"
  ],
  hdrs = ["include/bar.h"],
  includes = [ "src", "include" ],
)
)");

  {
    DWYUTestFixture tester(pp.project(), {.verbose = 2});
    tester.AddSource("some/path/include/bar.h", "");
    tester.AddSource("some/path/src/foo.h", "");
    tester.AddSource("some/path/src/subdir/foo.cc", R"(
#include "foo.h"   // include = ["src"] adds that to -I
#include "bar.h"   // include = ["include"] adds that to -I
)");
    tester.RunForTarget("//some/path:foo");
    EXPECT_THAT(tester.LogContent(), HasSubstr("-Isrc matched foo.h"));
    EXPECT_THAT(tester.LogContent(), HasSubstr("-Iinclude matched bar.h"));
  }
}

TEST(DWYUTest, FilesWithColonsInsideThemAreProperlyRecognized) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "foo",
  srcs = [
     "foo.cc",
     ":foo.h",
  ],
)
)");

  {
    DWYUTestFixture tester(pp.project(), {.verbose = 2});
    tester.AddSource("some/path/foo.h", "");
    tester.AddSource("some/path/foo.cc", R"(
#include "foo.h"
)");
    tester.RunForTarget("//some/path:foo");
  }
}

TEST(DWYUTest, ToplevelIncludesWithoutPrefixSlashWork) {
  ParsedProjectTestUtil pp;
  pp.Add("//", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"]
)
)");

  {
    DWYUTestFixture tester(pp.project(), {});
    tester.AddSource("foo.h", "");
    tester.AddSource("foo.cc", R"(
#include "foo.h"
)");
    tester.RunForTarget("//:foo");
  }
}

TEST(DWYUTest, HeaderIncludingIncFromSource) {
  ParsedProjectTestUtil pp;
  pp.Add("//path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.inc"],
  hdrs = ["foo.h"]
)
)");
  {
    DWYUTestFixture tester(pp.project(), {});
    tester.AddSource("path/foo.inc", "");
    tester.AddSource("path/foo.h", R"(
#include "path/foo.inc"  // should be recognized to come from own target
)");
    tester.RunForTarget("//path:foo");
  }
}

TEST(DWYUTest, FilesComingFromFilegroupsAreExpanded) {
  ParsedProjectTestUtil pp;
  pp.Add("//path", R"(
filegroup(
  name = "foo_hdrs",
  srcs = ["foo.h"],
)
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = [":foo_hdrs"],
)

filegroup(
  name = "quux_srcs",
  srcs = ["quux.cc"],
)
cc_library(
  name = "quux",
  srcs = [":quux_srcs"],
)
)");
  {
    DWYUTestFixture tester(pp.project(), {});
    tester.ExpectAdd(":foo");
    tester.AddSource("path/foo.h", "");
    tester.AddSource("path/quux.cc", R"(
#include "path/foo.h"
)");
    tester.RunForTarget("//path:quux");
  }
}

TEST(DWYUTest, ChooseMinimalDependencySetIfMultipleLibrariesProvideHeader) {
  ParsedProjectTestUtil pp;
  pp.Add("//path", R"(
cc_library(
  name = "allthethings",
  hdrs = ["foo.h", "bar.h"]
)

cc_library(
  name = "onlyfoo",
  hdrs = ["foo.h"]
)

cc_binary(
  name = "baz",
  srcs = ["baz.cc"],
  deps = [":allthethings"],  # should be sufficient
)
)");

  {
    DWYUTestFixture tester(pp.project(), {.verbose = 3});
    tester.AddSource("path/baz.cc", R"(
#include "path/bar.h"   // order dependent currently.
#include "path/foo.h"
)");
    tester.RunForTarget("//path:baz");
    EXPECT_THAT(tester.LogContent(), HasSubstr("✓ //path:allthethings"));
    EXPECT_THAT(tester.LogContent(), HasSubstr("- //path:onlyfoo"));
  }
}

TEST(DWYUTest, RequestUserGuidanceIfThereAreMultipleAlternatives) {
  // Sometimes, there are multiple libraries providing the same
  // header.

  ParsedProjectTestUtil pp;
  pp.Add("//path", R"(
cc_library(
  name = "foo-1",
  hdrs = ["foo.h"]
)

cc_library(
  name = "foo-2",
  hdrs = ["foo.h"]   # provides the _same_ header as foo-1
)

cc_library(
  name = "usefoo-1",
  srcs = ["usefoo-1.cc"],
  deps = [":foo-1"],    # choice of one of the alternatives
)

cc_library(
  name = "usefoo-2",
  srcs = ["usefoo-2.cc"],
  deps = [":foo-2"],    # choice of one of the alternatives
)

cc_library(
  name = "usefoo-all",
  srcs = ["usefoo-all.cc"],
  deps = [
     ":foo-1",
     ":foo-2",     # overconstrained, but will not be able to do anything about
  ],
)

cc_library(
  name = "usefoo-duplicate",
  srcs = ["usefoo-duplicate.cc"],
  deps = [
     ":foo-1",
     ":foo-1",    # duplicate
  ],
)

cc_library(
  name = "usefoo-undecided",
  srcs = ["usefoo-undecided.cc"],
  # No deps added. Bant will also not be able to help.
)
)");

  {  // Uses one of the libraries providing foo.h header. Satisfied.
    DWYUTestFixture tester(pp.project(), {.verbose = 3});
    // No expects of add, as "foo-1" is used and it provides header.
    tester.AddSource("path/usefoo-1.cc", R"(#include "path/foo.h")");
    tester.RunForTarget("//path:usefoo-1");
    EXPECT_THAT(tester.LogContent(), HasSubstr("✓ //path:foo-1"));
  }

  {  // Uses the other of the libraries providing foo.h header. Satisfied.
    DWYUTestFixture tester(pp.project(), {.verbose = 3});
    // No expects of add, as "foo-2" is used and it provides header.
    tester.AddSource("path/usefoo-2.cc", R"(#include "path/foo.h")");
    tester.RunForTarget("//path:usefoo-2");
    EXPECT_THAT(tester.LogContent(), HasSubstr("✓ //path:foo-2"));
  }

  {
    // Attempt to add same dependency twice.
    DWYUTestFixture tester(pp.project(), {});
    tester.AddSource("path/usefoo-duplicate.cc", R"(#include "path/foo.h")");
    tester.RunForTarget("//path:usefoo-duplicate");
    EXPECT_THAT(tester.LogContent(), HasSubstr("mentioned multiple times"));
  }

  {
    // Add _all_ dependencies that provide the same header. Maybe not intended ?
    DWYUTestFixture tester(pp.project(), {});
    tester.AddSource("path/usefoo-all.cc", R"(#include "path/foo.h")");
    tester.RunForTarget("//path:usefoo-all");
    EXPECT_THAT(tester.LogContent(), HasSubstr("by //path:foo-1 before"));
  }

  {  // Known dependencies, but they are alternatives. Need to delegate to user.
    DWYUTestFixture tester(pp.project(), {});
    // No expects of add, as it needs to be a user choice.
    tester.AddSource("path/usefoo-undecided.cc", R"(#include "path/foo.h")");
    tester.RunForTarget("//path:usefoo-undecided");
    EXPECT_THAT(tester.LogContent(), HasSubstr("Alternatives are"));
  }
}

// A typical situation: using an alias to point to a new library, and mark
// that alias deprecated. Even though the library and the alias are now
// an alternative, this keeps the new library the only viable alternative.
TEST(DWYUTest, ChooseNonDeprecatedAlternative) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/lib", R"(
alias(
  name = "deprecated_foo",
  actual = ":new_foo",
  deprecation = "Deprecation note so not considered an alternative",
)

cc_library(
  name = "new_foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
)
)");

  pp.Add("//user", R"(
cc_binary(
   name = "hello",
   srcs = ["hello.cc"],
   deps = ["//some/lib:deprecated_foo"],
)
)");

  {
    // We have two alternatives, but since we can sort out the deprecated one,
    // we expect a definitive add of the non-deprecated version.
    DWYUTestFixture tester(pp.project(), {.verbose = 3});
    tester.ExpectAdd("//some/lib:new_foo");
    tester.ExpectRemove("//some/lib:deprecated_foo");
    tester.AddSource("user/hello.cc", R"(#include "some/lib/foo.h")");
    tester.RunForTarget("//user:hello");
    EXPECT_THAT(tester.LogContent(),
                HasSubstr("avoid if possible: Deprecation"));
  }
}

// Similar to deprecation, the tag = ["avoid_dep"] helps filter out.
// (note: example similar to ChooseNonDeprecatedAlternative)
TEST(DWYUTest, ChooseNonAvoidDepAlternative) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/lib", R"(
alias(
  name = "to_avoid_foo",
  actual = ":new_foo",
  tags = ["avoid_dep"],
)

cc_library(
  name = "new_foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
)
)");

  pp.Add("//user", R"(
cc_binary(
   name = "hello",
   srcs = ["hello.cc"],
   deps = ["//some/lib:to_avoid_foo"],
)
)");

  {
    DWYUTestFixture tester(pp.project(), {.verbose = 3});
    tester.ExpectAdd("//some/lib:new_foo");
    tester.ExpectRemove("//some/lib:to_avoid_foo");
    tester.AddSource("user/hello.cc", R"(#include "some/lib/foo.h")");
    tester.RunForTarget("//user:hello");
    EXPECT_THAT(tester.LogContent(), HasSubstr("avoid if possible: avoid_dep"));
  }
}

TEST(DWYUTest, Add_MissingDependencyInDifferentPackage) {
  ParsedProjectTestUtil pp;
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
  # needed //lib/path:foo dependency not given
)
)");

  DWYUTestFixture tester(pp.project(), {});
  tester.ExpectAdd("//lib/path:foo");
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//some/path:bar");
}

TEST(DWYUTest, DoNotAdd_IfNotVisible) {
  ParsedProjectTestUtil pp;
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  visibility = ["//visibility:private"],  # Should not link outside
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
  # needed //lib/path:foo dependency not given, but it is private
)
)");

  DWYUTestFixture tester(pp.project(), {.verbose = 3});
  // No add expected.
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//some/path:bar");
  EXPECT_THAT(tester.LogContent(), HasSubstr("not matching visibility"));
}

TEST(DWYUTest, DoNotAdd_IfVisibilityDisallowedByPackageGroup) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/package", R"(
package_group(
  name = "group",
  packages = [
    "//something/...",
    # //some/path not allowed
  ],
)
)");  //
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  visibility = ["//some/package:group"],
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
)
)");

  DWYUTestFixture tester(pp.project(), {.verbose = 3});
  // no add happening
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//some/path:bar");
  EXPECT_THAT(tester.LogContent(), HasSubstr("not matching visibility"));
}

// Note, this is essentially the same as before, except that we now allow
// some/path/... in package group
TEST(DWYUTest, Add_IfVisibilityAllowedByPackageGroup) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/package", R"(
package_group(
  name = "group",
  packages = [
    "//something/...",
    "//some/path/...",
  ],
)
)");  //
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  visibility = ["//some/package:group"],
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
)
)");

  DWYUTestFixture tester(pp.project(), {});
  tester.ExpectAdd("//lib/path:foo");  // allowed
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//some/path:bar");
}

TEST(DWYUTest, VisibilityMatchesBasicNegativePattern) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/package", R"(
package_group(
  name = "group",
  packages = [
    "//something/...",
    "//some/...",
    "-//some/path/...",
  ],
)
)");  //
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  visibility = ["//some/package:group"],
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
)
)");

  DWYUTestFixture tester(pp.project(), {});
  // Nothing added, as //some/path/... is explicitly exlcuded
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//some/path:bar");
}

TEST(DWYUTest, VisibilityIsFollowedThroughIncludeIndirection) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/package", R"(
package_group(
  name = "some_path_group",
  packages = [
    "//some/path/...",
  ]
)

package_group(
  name = "group",
  includes = [ ":some_path_group" ],
  packages = [
    "//something/...",
  ],
)
)");  //
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  visibility = ["//some/package:group"],
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
)
)");

  DWYUTestFixture tester(pp.project(), {});
  tester.ExpectAdd("//lib/path:foo");  // allowed
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//some/path:bar");
}

TEST(DWYUTest, VisibilityIsFollowedThroughInPackageGroupsMentioned) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/package", R"(
package_group(
  name = "some_path_group",
  packages = [
    "//some/path/...",
  ]
)

package_group(
  name = "group",
  packages = [
    "//something/...",
    ":some_path_group",  # should be dealt like include
  ],
)
)");  //
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  visibility = ["//some/package:group"],
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
)
)");

  DWYUTestFixture tester(pp.project(), {});
  tester.ExpectAdd("//lib/path:foo");  // allowed
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//some/path:bar");
}

TEST(DWYUTest, AllowVisibilityFromVariable) {
  for (const bool private_visibility : {false, true}) {
    ParsedProjectTestUtil pp;
    pp.Add("//lib/path",
           absl::StrFormat(R"(
VISIBILITY_VARIABLE = "//visibility:%s"
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  visibility = [ VISIBILITY_VARIABLE ],
)
)",
                           private_visibility ? "private" : "public"));

    pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
)
)");

    pp.ElaborateAll();  // Expand variables
    DWYUTestFixture tester(pp.project(), {});
    // Library is only added if now private
    if (!private_visibility) {
      tester.ExpectAdd("//lib/path:foo");
    }
    tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
    tester.RunForTarget("//some/path:bar");
  }
}

// Often aliasse are used to redirect to otherwise private libraries. The
// visibility of the alias determines if it can be linked.
TEST(DWYUTest, VisibilityOfAliasAllowsToBeLinkedEvenIfTargetIsNotVisible) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/lib", R"(
alias(
  name = "visible_foo_alias",
  actual = ":private_foo",
  visibility = ["//visibility:public"],   # points to lib with correct header
)

cc_library(
  name = "private_foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  visibility = ["//visibility:private"],   # can't use this directly
)
)");

  pp.Add("//user", R"(
cc_binary(
   name = "hello",
   srcs = ["hello.cc"],
)
)");

  {
    DWYUTestFixture tester(pp.project(), {.verbose = 3});
    tester.ExpectAdd("//some/lib:visible_foo_alias");
    tester.AddSource("user/hello.cc", R"(#include "some/lib/foo.h")");
    tester.RunForTarget("//user:hello");
    EXPECT_THAT(tester.LogContent(), HasSubstr("private_foo (avoid"));
  }
}

// If we can't expand, err on the 'public' side.
TEST(DWYUTest, ConsiderUnknownVisibilityVariableToAllowPublic) {
  ParsedProjectTestUtil pp;
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  visibility = [ UNKNOWN_VARIABLE ],
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
)
)");

  pp.ElaborateAll();  // Expand variables, but UNKNOWN_VARIABLE will stay as-is
  DWYUTestFixture tester(pp.project(), {});
  tester.ExpectAdd("//lib/path:foo");  // Added, because unknown means: public
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//some/path:bar");
}

TEST(DWYUTest, Add_AlwaysConsiderLocalPackagesVisible) {
  ParsedProjectTestUtil pp;
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  visibility = ["//some/package:__pkg__"],   # but we should still see locally
)

cc_library(
  name = "bar",
  srcs = ["bar.cc"],
)
)");

  DWYUTestFixture tester(pp.project(), {});
  tester.ExpectAdd(":foo");
  tester.AddSource("lib/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//lib/path:bar");
}

TEST(DWYUTest, DoNotAdd_IfNotVisibleDueToDefaultVisibility) {
  ParsedProjectTestUtil pp;
  pp.Add("//lib/path", R"(
package(
  default_visibility = ["//visibility:private"],  # :foo will inherit that
)

cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
  # needed //lib/path:foo dependency not given, but it is private
)
)");

  DWYUTestFixture tester(pp.project(), {.verbose = 3});
  // No add expected.
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//some/path:bar");
  EXPECT_THAT(tester.LogContent(),
              HasSubstr("not matching default_visibility"));
}

TEST(DWYUTest, DoNotAdd_IfTestonlyMismatch) {
  ParsedProjectTestUtil pp;
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  testonly = True,
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
  # needed //lib/path:foo dependency not given, but it is testonly.
)
)");

  DWYUTestFixture tester(pp.project(), {});
  // No add expected.
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//some/path:bar");
  EXPECT_THAT(tester.LogContent(), HasSubstr("is marked testonly"));
}

TEST(DWYUTest, Remove_SuperfluousDependency) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
)

cc_library(
  name = "bar",
  srcs = ["bar.cc"],
  hdrs = ["bar.h"],
)

cc_library(
  name = "baz",
  srcs = ["baz.cc"],
  deps = [
    ":foo",
    ":bar"  # random text dwyu that contains word keep
  ],
)
)");

  DWYUTestFixture tester(pp.project(), {.verbose = 3});
  tester.ExpectRemove(":foo");
  // :bar should be removed, but is kept due to comment
  tester.AddSource("some/path/baz.cc", "/* no include */");
  tester.RunForTarget("//some/path:baz");
  EXPECT_THAT(tester.LogContent(), HasSubstr("contains word keep"));
}

TEST(DWYUTest, DoNotRemove_IfThereIsSourceThatCanNotBeRead) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  hdrs = ["bar.h"],
)

cc_library(
  name = "foo",
  srcs = [
     "notfound.cc",
  ],
  deps = [
     ":bar",  # dependency that we can't know if safe to remove.
  ],
)
)");

  {
    DWYUTestFixture tester(pp.project(), {.verbose = 2});
    // Explicitly not adding the source.
    // We don't know if we can remove :bar dependency.
    tester.RunForTarget("//some/path:foo");
    EXPECT_THAT(tester.LogContent(), HasSubstr("Can not read source"));
    EXPECT_THAT(tester.LogContent(), HasSubstr("looks superfluous"));
  }
}

TEST(DWYUTest, DoNotRemove_IfThereIsAHeaderThatIsUnaccounted) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
)

cc_library(
  name = "bar",     # a library without headers, should be consider keep
  srcs = ["foo.cc"],
)

cc_library(
  name = "baz",
  srcs = ["baz.cc"],
  hdrs = ["baz.h"],
)

cc_library(
  name = "quux",
  srcs = ["quux.cc"],
  deps = [
       ":foo",     # Not nominally needed, but we can't be sure to remove.
       ":bar",     # considered to keep due to no headers
       ":baz",     # buildcleaner:keep
   ],
)
)");

  DWYUTestFixture tester(pp.project(), {.verbose = 2});
  tester.AddSource("some/path/quux.cc", R"(
#include "some/path/unaccounted-header.h"
)");
  tester.RunForTarget("//some/path:quux");

  EXPECT_THAT(tester.LogContent(), HasSubstr("unknown provider"));
  EXPECT_THAT(tester.LogContent(),
              HasSubstr(":foo dependency looks superfluous"));

  // Now that we know we can't report due to unknown provider, we should
  // we should NOT want to report other reasons, as we generally don't do
  // any removals on that target now.
  EXPECT_THAT(tester.LogContent(),
              Not(HasSubstr(":bar dependency looks superfluous")));
  // .. or keep
  EXPECT_THAT(tester.LogContent(),
              Not(HasSubstr(":baz dependency looks superfluous")));
}

TEST(DWYUTest, DoNotRemove_AlwayslinkDependency) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  alwayslink = True
)

cc_library(
  name = "bar",
  srcs = ["bar.cc"],
  deps = [":foo"],
)
)");

  DWYUTestFixture tester(pp.project(), {.verbose = 3});
  tester.AddSource("some/path/bar.cc", "/* no include */");
  tester.RunForTarget("//some/path:bar");
  EXPECT_THAT(tester.LogContent(), HasSubstr("due to alwayslink: 'True'"));
}

TEST(DWYUTest, DoNotRemove_TaggedWith_keep_dep_Dependency) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  tags = ["keep_dep"],
)

cc_library(
  name = "bar",
  srcs = ["bar.cc"],
  deps = [":foo"],
)
)");

  DWYUTestFixture tester(pp.project(), {.verbose = 3});
  tester.AddSource("some/path/bar.cc", "/* no include */");
  tester.RunForTarget("//some/path:bar");
  EXPECT_THAT(tester.LogContent(), HasSubstr("due to tag: 'keep_dep'"));
}

TEST(DWYUTest, DoNotRemove_LibraryWithoutHeaderConsideredDependencyToKeep) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  # no headers exported. So if referenced, we consider it alwayslink
)

cc_library(
  name = "bar",
  srcs = ["bar.cc"],
  hdrs = [],   # list there, but empty.
)

cc_library(
  name = "baz",
  srcs = ["baz.cc"],
  hdrs = ["baz.h"],   # Provides a header that is not included -> Remove lib
)

cc_library(
  name = "quux",
  srcs = ["quux.cc"],
  deps = [
    ":foo",
    ":bar",
    ":baz",
  ]
)
)");

  DWYUTestFixture tester(pp.project(), {.verbose = 3});
  tester.AddSource("some/path/quux.cc", "/* no include */");

  tester.ExpectRemove(":baz");  // Only one expected to be removed.
  tester.RunForTarget("//some/path:quux");
  EXPECT_THAT(tester.LogContent(), HasSubstr("due to empty hdrs"));
}

TEST(DWYUTest, DefinesAreInheritedFromLibraries) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  hdrs = ["bar.h"],
  defines = ["HELLO_WORLD"],
)

cc_library(
  name = "foo",
  srcs = [ "foo.cc"],
  deps = [
     ":bar"
  ],
)
)");

  {
    DWYUTestFixture tester(pp.project(), {.verbose = 3});
    tester.AddSource("some/path/foo.cc", R"(
#ifdef HELLO_WORLD  // only if we properly eval this...
#include "some/path/bar.h"  // ... we see this include and won't remove :bar
#endif
)");
    tester.RunForTarget("//some/path:foo");
    EXPECT_THAT(tester.LogContent(),
                HasSubstr("PP: in condition #ifdef HELLO_WORLD"));
  }
}

TEST(DWYUTest, COptsAreNotInheritedFromLibraries) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  hdrs = ["bar.h"],
  defines = ["SOME_UNRELATED_DEFINE"],
  copts = ["-DHELLO_WORLD"],
)

cc_library(
  name = "foo",
  srcs = [ "foo.cc"],
  deps = [
     ":bar"
  ],
)
)");

  {
    DWYUTestFixture tester(pp.project(), {.verbose = 1});
    tester.AddSource("some/path/foo.cc", R"(
#ifdef HELLO_WORLD  // this one we should not see
#include "some/path/bar.h"  // ... thus we should proceed to remove this
#endif
)");
    tester.ExpectRemove(":bar");
    tester.RunForTarget("//some/path:foo");
  }
}

TEST(DWYUTest, MakeSureWeSeeLocalDefines) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  hdrs = ["bar.h"],
)

cc_library(
  name = "foo",
  srcs = [ "foo.cc"],
  local_defines = ["SHOULD_INCLUDE_BAR"],
)
)");

  {
    DWYUTestFixture tester(pp.project(), {.verbose = 1});
    tester.AddSource("some/path/foo.cc", R"(
#ifdef SHOULD_INCLUDE_BAR
#include "some/path/bar.h"  // ... thus we should proceed to remove this
#endif
)");
    tester.ExpectAdd(":bar");
    tester.RunForTarget("//some/path:foo");
  }
}

TEST(DWYUTest, Add_ProtoLibraryForProtoInclude) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
proto_library(
  name = "foo_proto",
  srcs = ["foo.proto", "baz.proto"],
)

cc_proto_library(
  name = "foo_proto_lib",
  deps = [":foo_proto"],
)

cc_library(
  name = "bar",
  srcs = ["bar.cc"],
)
)");

  DWYUTestFixture tester(pp.project(), {.verbose = 3});
  tester.AddSource("some/path/bar.cc", R"(
#include "some/path/baz.pb.h"
)");
  tester.ExpectAdd(":foo_proto_lib");
  tester.RunForTarget("//some/path:bar");
  EXPECT_THAT(tester.LogContent(), HasSubstr("- //some/path:foo_proto_lib"));
}

TEST(DWYUTest, Add_ProtoLibraryForProtoIncludeThatActsAsDependencyForwarder) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
proto_library(
  name = "foo_proto",
  srcs = ["foo.proto"],
)

proto_library(
  name = "bar_proto",
  srcs = ["bar.proto"],
)

proto_library(
  name = "baz_proto",
  deps = [":bar_proto"],
)

proto_library(
  name = "all_proto",
  deps = [
    ":foo_proto",
    ":baz_proto",
  ]
)

cc_proto_library(
  name = "all_proto_lib",
  deps = [":all_proto"],
)

cc_library(
  name = "bar",
  srcs = ["bar.cc"],
)
)");

  DWYUTestFixture tester(pp.project(), {});
  tester.AddSource("some/path/bar.cc", R"(
#include "some/path/bar.pb.h"
)");
  tester.ExpectAdd(":all_proto_lib");
  tester.RunForTarget("//some/path:bar");
}

TEST(DWYUTest, Add_ProtoLibraryForProtoIncludeNotADependencyForwarder) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
proto_library(
  name = "foo_proto",
  srcs = ["foo.proto"],
)

cc_proto_library(
  name = "foo_proto_lib",
  deps = [":foo_proto"],
)

proto_library(
  name = "bar_proto",
  srcs = ["bar.proto"],
)

cc_proto_library(
  name = "bar_proto_lib",
  deps = [":bar_proto"],
)

proto_library(
  name = "baz_proto",
  deps = [":bar_proto"],
)

cc_proto_library(
  name = "baz_proto_lib",
  srcs = [":baz_proto"],
)

proto_library(
  name = "just_myproto_proto",
  srcs = ["myproto.proto"],  # Here, we have a source
  deps = [
    ":foo_proto",            # ... so these are just considered private deps
    ":baz_proto",            # ... and are not forwarded
  ]
)

cc_proto_library(
  name = "just_myproto_proto_lib",
  deps = [":just_myproto_proto"],
)

cc_library(
  name = "bar",
  srcs = ["bar.cc"],
  deps = [
     # since we're not using myproto.pb.h, this library needs to be removed.
    ":just_myproto_proto_lib"
  ],
)
)");

  DWYUTestFixture tester(pp.project(), {});
  tester.AddSource("some/path/bar.cc", R"(
#include "some/path/foo.pb.h"
#include "some/path/bar.pb.h"
)");
  // We don't exepct :just_myproto_proto_lib to be added, which was correct inc
  // Add_ProtoLibraryForProtoIncludeThatActsAsDependencyForwarder.
  // But since :just_myproto_proto is not a forwarding lib (it has its own
  // srcs=[]), we have to actually expet the indivitual foo and bar proto lib
  tester.ExpectAdd(":foo_proto_lib");
  tester.ExpectAdd(":bar_proto_lib");
  tester.ExpectRemove(":just_myproto_proto_lib");
  tester.RunForTarget("//some/path:bar");
}

TEST(DWYUTest, Add_ProtoGrpcLibraryForProtoInclude) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
proto_library(
  name = "foo_proto",
  srcs = ["foo.proto", "baz.proto"],
)

cc_grpc_library(              # GRPC form of a proto library.
  name = "grpc_foo",
  srcs = [":foo_proto"],
)

cc_library(
  name = "bar",
  srcs = ["bar.cc"],
)
)");

  DWYUTestFixture tester(pp.project(), {});
  tester.AddSource("some/path/bar.cc", R"(
#include "some/path/baz.grpc.pb.h"
)");
  tester.ExpectAdd(":grpc_foo");
  tester.RunForTarget("//some/path:bar");
}

TEST(DWYUTest, Remove_UnncessaryProtoLibrary) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
proto_library(
  name = "foo_proto",
  srcs = ["foo.proto", "baz.proto"],
)

cc_proto_library(
  name = "foo_proto_lib",
  deps = [":foo_proto"],
)

cc_library(
  name = "bar",
  srcs = ["bar.cc"],
  deps = [":foo_proto_lib"],
)
)");

  DWYUTestFixture tester(pp.project(), {.verbose = 3});
  tester.AddSource("some/path/bar.cc", "/* no include */");
  tester.ExpectRemove(":foo_proto_lib");
  tester.RunForTarget("//some/path:bar");
  // no log output, as we have not seen any header.
}

// In absl/strings:string_view, there is the string_view.h exported.
// But it is _also_ exported by absl/strings:strings but with the remark
// that this is only there for backward compatibility. In fact, it is
// mentioned twice, in hdrs and in textual_hdrs.
// We use this fact for bant to correctly suggest the :string_view
// library. Below, situation re-created.
TEST(DWYUTest, Add_AbslStringViewWorkaround) {
  ParsedProjectTestUtil pp;
  pp.Add("//foo/absl/strings", R"(   # iff packages suffix is absl/strings
cc_library(
  name = "string_view",
  hdrs = ["string_view.h"]  # The actual place definining header
)

cc_library(
  name = "strings",
  hdrs = [
    "str_cat.h",
    "string_view.h"         # But also defined here
  ],
  textual_hdrs = [
    "string_view.h"         # ... also here. This is how we detect.
  ],
)

cc_binary(
  name = "string-user",
  srcs = ["string-user.cc"],
  # expecting deps added
)
)");

  {
    DWYUTestFixture tester(pp.project(), {});
    tester.ExpectAdd(":strings");
    tester.ExpectAdd(":string_view");
    tester.AddSource("foo/absl/strings/string-user.cc", R"(
#include "foo/absl/strings/str_cat.h"
#include "foo/absl/strings/string_view.h"
)");
    tester.RunForTarget("//foo/absl/strings:string-user");
  }
}

TEST(DWYUTest, Remove_SuperfluousProtoLibraryDependency) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
proto_library(
  name = "foo_proto",
  srcs = ["foo.proto"],
)

proto_library(
  name = "bar_proto",
  srcs = ["bar.proto"],
)

proto_library(
  name = "baz_proto",
  srcs = ["baz.proto"],
  deps = [
    ":foo_proto",
    ":bar_proto",
  ],
)
)");

  DWYUTestFixture tester(pp.project(), {});
  tester.ExpectRemove(":bar_proto");  // not imported
  tester.AddSource("some/path/baz.proto", R"(
syntax = "proto3";
import "some/path/foo.proto";
)");
  tester.RunForTarget("//some/path:baz_proto");
}

TEST(DWYUTest, Add_MissingProtoLibraryDependency) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
proto_library(
  name = "foo_proto",
  srcs = ["foo.proto"],
)

proto_library(
  name = "baz_proto",
  srcs = ["baz.proto"],
  # Missing dep on :foo_proto
)
)");

  DWYUTestFixture tester(pp.project(), {});
  tester.ExpectAdd(":foo_proto");
  tester.AddSource("some/path/baz.proto", R"(
syntax = "proto3";
import "some/path/foo.proto";
)");
  tester.RunForTarget("//some/path:baz_proto");
}

TEST(DWYUTest, Add_PrefersLocalStratumOverExternal) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
)
)");

  pp.Add("@other_workspace//some/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
)
)");

  pp.Add("//user", R"(
cc_binary(
  name = "bar",
  srcs = ["bar.cc"],
)
)");

  DWYUTestFixture tester(pp.project(), {});
  // Even though both provide 'some/path/foo.h', we expect the local one to be
  // preferred.
  tester.ExpectAdd("//some/path:foo");
  tester.AddSource("user/bar.cc", R"(
#include "some/path/foo.h"
)");
  tester.RunForTarget("//user:bar");
}

TEST(DWYUTest, Add_TestonlyDependencyAllowedForTestTargets) {
  ParsedProjectTestUtil pp;
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  testonly = True,
)
)");

  pp.Add("//some/path", R"(
cc_test(
  name = "my_test",
  srcs = ["my_test.cc"],
)

cc_library(
  name = "my_test_lib",
  srcs = ["my_test_lib.cc"],
  testonly = True,
)
)");

  {
    DWYUTestFixture tester(pp.project(), {});
    tester.ExpectAdd("//lib/path:foo");
    tester.AddSource("some/path/my_test.cc", R"(#include "lib/path/foo.h")");
    tester.RunForTarget("//some/path:my_test");
  }
  {
    DWYUTestFixture tester(pp.project(), {});
    tester.ExpectAdd("//lib/path:foo");
    tester.AddSource("some/path/my_test_lib.cc",
                     R"(#include "lib/path/foo.h")");
    tester.RunForTarget("//some/path:my_test_lib");
  }
}

TEST(DWYUTest, Add_SkipsDependencyCheckIfBantSkipIsSet) {
  ParsedProjectTestUtil pp;
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
  bant_skip_dependency_check = True,
)
)");

  DWYUTestFixture tester(pp.project(), {});
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  // Since skip dependency check is set, we do NOT expect an Add() for :foo
  tester.RunForTarget("//some/path:bar");
}

TEST(DWYUTest, Add_SkipsDependencyCheckIf_nofixdeps_tagIsSet) {
  ParsedProjectTestUtil pp;
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
  tags = ["nofixdeps"],  # Semantically the same as bant_skip_dependency_check
)
)");

  DWYUTestFixture tester(pp.project(), {});
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  // Since tag "nofixdeps" is provided, we do NOT expect an Add() for :foo
  tester.RunForTarget("//some/path:bar");
}

// If something is deprecated, we usually would not consider it. However, if
// this is the only viable dependency, use that.
TEST(DWYUTest, Add_ExclusivelyDeprecatedDependency) {
  ParsedProjectTestUtil pp;
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  deprecation = "Do not use foo.",
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
)
)");

  DWYUTestFixture tester(pp.project(), {.verbose = 3});
  tester.ExpectAdd("//lib/path:foo");
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//some/path:bar");
  const std::string log = tester.LogContent();
  EXPECT_THAT(log, HasSubstr("//lib/path:foo is the only suitable dependency"));
  EXPECT_THAT(log, HasSubstr("avoid if possible: Do not use foo."));
}

TEST(DWYUTest, ReplaceDeprecatedDependencyWithNonDeprecatedAlternative) {
  ParsedProjectTestUtil pp;
  pp.Add("//lib/path", R"(
cc_library(
  name = "deprecated_foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  deprecation = "Use new_foo instead",
)

cc_library(
  name = "new_foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
  deps = ["//lib/path:deprecated_foo"],
)
)");

  DWYUTestFixture tester(pp.project(), {.verbose = 3});
  tester.ExpectRemove("//lib/path:deprecated_foo");
  tester.ExpectAdd("//lib/path:new_foo");
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//some/path:bar");
  EXPECT_THAT(tester.LogContent(), HasSubstr(":deprecated_foo (avoid"));
}

TEST(DWYUTest, BracketIncludeHandling) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "foo",
  hdrs = ["foo.h"],
)

cc_library(
  name = "bar",
  hdrs = ["bar.h"],
)

cc_library(
  name = "baz",
  srcs = ["baz.cc"],
  deps = [
    ":foo",    # we're using dep, but with angle brackets.
  ],
)
)");

  for (BracketIncHandling bracket_inc : {BracketIncHandling::kIgnore,       //
                                         BracketIncHandling::kAcknowledge,  //
                                         BracketIncHandling::kValidate}) {
    DWYUTestFixture tester(pp.project(),
                           {.verbose = 1, .dwyu_bracket_include = bracket_inc});
    tester.AddSource("some/path/foo.h", "");
    tester.AddSource("some/path/bar.h", "");
    tester.AddSource("some/path/baz.cc", R"(
#include <some/path/foo.h>  // angle-bracketed reference to project header
#include <some/path/bar.h>
)");

    if (bracket_inc == BracketIncHandling::kIgnore) {
      tester.ExpectRemove(":foo");
    }
    if (bracket_inc == BracketIncHandling::kValidate) {
      tester.ExpectAdd(":bar");
    }
    tester.RunForTarget("//some/path:baz");

    if (bracket_inc == BracketIncHandling::kValidate) {
      // In validate mode, we loudly complain to steer project towards fix.
      const std::string log = tester.LogContent();
      EXPECT_THAT(log, HasSubstr("quote-style \"some/path/foo.h"));
      EXPECT_THAT(log, HasSubstr("quote-style \"some/path/bar.h"));
    }
  }
}

}  // namespace
}  // namespace bant
