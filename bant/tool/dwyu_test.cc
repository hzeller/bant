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

#include "bant/tool/dwyu.h"

#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/parsed-project_testutil.h"
#include "bant/frontend/source-locator.h"
#include "bant/output-format.h"
#include "bant/session.h"
#include "bant/tool/dwyu-internal.h"
#include "bant/tool/edit-callback_testutil.h"
#include "bant/types-bazel.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::ElementsAre;
using ::testing::HasSubstr;

namespace bant {

static LineColumn PosOfPart(const NamedLineIndexedContent &src,
                            const std::vector<std::string_view> &parts,
                            size_t i) {
  CHECK(i <= parts.size());
  return src.GetLocation(parts[i]).line_column_range.start;
}

// Inception deception:
// Well, the following with a string in a string will create a warning if
// running bant on bant becaue the include in string is seen as toplevel inc.
// So, to avoid that, the include is actually an legitimate bant include which
// makes bant happy (until we start warning that the same header is included
// twice).
TEST(DWYUTest, HeaderFilesAreExtracted) {
  constexpr std::string_view kTestContent = R"(  // line 0
/* some ignored text in line 1 */
#include "CaSe-dash_underscore.h"
#include <should_not_be_extracted>
// #include "also-not-extracted.h"
   #include "but-this.h"
#include "with/suffix.hh"      // other ..
#include "with/suffix.pb.h"
#include "with/suffix.inc"     // .. common suffices
R"(
#include "bant/tool/dwyu.h"   // include embedded in string ignored.
")
#include    "w/space.h"        // even strange spacing should work
#include /* foo */ "this-is-silly.h"  // Some things are too far :)
#include "../dotdot.h"         // mmh, who is doing this ?
#include "more-special-c++.h"  // other characters used.
)";
  NamedLineIndexedContent scanned_src("<text>", kTestContent);
  const auto includes = ExtractCCIncludes(&scanned_src);
  EXPECT_THAT(includes, ElementsAre("CaSe-dash_underscore.h", "but-this.h",
                                    "with/suffix.hh", "with/suffix.pb.h",
                                    "with/suffix.inc", "w/space.h",
                                    "../dotdot.h", "more-special-c++.h"));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 0), (LineColumn{2, 10}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 1), (LineColumn{5, 13}));

  EXPECT_EQ(PosOfPart(scanned_src, includes, 2), (LineColumn{6, 10}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 3), (LineColumn{7, 10}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 4), (LineColumn{8, 10}));
  EXPECT_EQ(PosOfPart(scanned_src, includes, 5), (LineColumn{12, 13}));
}

namespace {

// A DWYUGenerator with a mocked-out way to extract the sources.
class TestableDWYUGenerator : public bant::DWYUGenerator {
 public:
  using DWYUGenerator::DWYUGenerator;

  void AddSource(std::string_view name, std::string_view content) {
    source_content_[name] = content;
  }

 protected:
  std::optional<SourceFile> TryOpenFile(std::string_view source_file) override {
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
  explicit DWYUTestFixture(const ParsedProject &project)
      : session_{&log_messages_, &log_messages_, true, OutputFormat::kNative},
        dwyu_(session_, project, edit_expector_.checker()){};

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
    dwyu_.CreateEditsForPattern(*pattern_or);
  }

  std::string LogContent() {
    log_content_requested_ = true;
    return log_messages_.str();
  }

 private:
  std::stringstream log_messages_;
  Session session_;
  EditExpector edit_expector_;
  TestableDWYUGenerator dwyu_;
  bool log_content_requested_{false};
};
}  // namespace

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
)");

  {
    DWYUTestFixture tester(pp.project());
    tester.ExpectAdd(":foo");
    tester.AddSource("some/path/bar.h", "");
    tester.AddSource("some/path/bar.cc", R"(
#include "some/path/foo.h"
#include "some/path/bar.h"
)");
    tester.RunForTarget("//some/path:bar");
  }

  {  // Files relative to current directory are properly handled.
    DWYUTestFixture tester(pp.project());
    tester.ExpectAdd(":foo");
    tester.AddSource("some/path/bar.h", "");
    tester.AddSource("some/path/bar.cc", R"(
#include "foo.h"
)");
    tester.RunForTarget("//some/path:bar");
    EXPECT_THAT(tester.LogContent(), HasSubstr("Consider FQN"));
  }

  // Fuzzy matching. We match files from the suffix so as a fallback
  // we allow for matching that.
  {  // Files that match full path but are longer are guessed to belong
    DWYUTestFixture tester(pp.project());
    tester.ExpectAdd(":foo");
    tester.AddSource("some/path/bar.h", "");
    tester.AddSource("some/path/bar.cc", R"(
#include "external/project/some/path/foo.h"
)");
    tester.RunForTarget("//some/path:bar");
    EXPECT_THAT(tester.LogContent(), HasSubstr("provides shorter same-suffix"));
  }

  {  // Files that are somewhat shorter are also matched.
    DWYUTestFixture tester(pp.project());
    tester.ExpectAdd(":foo");
    tester.AddSource("some/path/bar.h", "");
    tester.AddSource("some/path/bar.cc", R"(
#include "path/foo.h"
)");
    tester.RunForTarget("//some/path:bar");
    EXPECT_THAT(tester.LogContent(), HasSubstr("provides longer same-suffix"));
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
    DWYUTestFixture tester(pp.project());
    // No expects of add, as "foo-1" is used and it provides header.
    tester.AddSource("path/usefoo-1.cc", R"(#include "path/foo.h")");
    tester.RunForTarget("//path:usefoo-1");
  }

  {  // Uses the other of the libraries providing foo.h header. Satisfied.
    DWYUTestFixture tester(pp.project());
    // No expects of add, as "foo-2" is used and it provides header.
    tester.AddSource("path/usefoo-2.cc", R"(#include "path/foo.h")");
    tester.RunForTarget("//path:usefoo-2");
  }

  {
    // Attempt to add same dependency twice.
    DWYUTestFixture tester(pp.project());
    tester.AddSource("path/usefoo-duplicate.cc", R"(#include "path/foo.h")");
    tester.RunForTarget("//path:usefoo-duplicate");
    EXPECT_THAT(tester.LogContent(), HasSubstr("mentioned multiple times"));
  }

  {
    // Add _all_ dependencies that provide the same header. Maybe not intended ?
    DWYUTestFixture tester(pp.project());
    tester.AddSource("path/usefoo-all.cc", R"(#include "path/foo.h")");
    tester.RunForTarget("//path:usefoo-all");
    EXPECT_THAT(tester.LogContent(), HasSubstr("by dependency //path:foo-1"));
  }

  {  // Known dependencies, but they are alternatives. Need to delegate to user.
    DWYUTestFixture tester(pp.project());
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
  deprecation = "This note makes sure it is not considered an alternative",
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
    DWYUTestFixture tester(pp.project());
    tester.ExpectAdd("//some/lib:new_foo");
    tester.ExpectRemove("//some/lib:deprecated_foo");
    tester.AddSource("user/hello.cc", R"(#include "some/lib/foo.h")");
    tester.RunForTarget("//user:hello");
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

  DWYUTestFixture tester(pp.project());
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

  DWYUTestFixture tester(pp.project());
  // No add expected.
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//some/path:bar");
}

// We don't handle package groups properly yet, so should be treated
// as //visibility:public
TEST(DWYUTest, Add_IfVisibilityIsPackageGroup) {
  ParsedProjectTestUtil pp;
  pp.Add("//lib/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"],
  visibility = ["//some/package:group"],  # Should be considered public for now
)
)");

  pp.Add("//some/path", R"(
cc_library(
  name = "bar",
  srcs = ["bar.cc"],
)
)");

  DWYUTestFixture tester(pp.project());
  tester.ExpectAdd("//lib/path:foo");  // until we understand package groups
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

  DWYUTestFixture tester(pp.project());
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

  DWYUTestFixture tester(pp.project());
  // No add expected.
  tester.AddSource("some/path/bar.cc", R"(
#include "lib/path/foo.h"
)");
  tester.RunForTarget("//some/path:bar");
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
  deps = [":foo"],
)
)");

  DWYUTestFixture tester(pp.project());
  tester.ExpectRemove(":foo");
  tester.AddSource("some/path/bar.cc", "/* no include */");
  tester.RunForTarget("//some/path:bar");
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
  name = "bar",
  srcs = ["bar.cc"],
  deps = [":foo"],   # Not nominally needed, but we can't be sure to remove.
)
)");

  DWYUTestFixture tester(pp.project());
  tester.AddSource("some/path/bar.cc", R"(
#include "some/path/unaccounted-header.h"
)");
  tester.RunForTarget("//some/path:bar");
  EXPECT_THAT(tester.LogContent(),
              HasSubstr("unknown provider for some/path/unaccounted-header.h"));
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

  DWYUTestFixture tester(pp.project());
  tester.AddSource("some/path/bar.cc", "/* no include */");
  tester.RunForTarget("//some/path:bar");
}

TEST(DWYUTest, DoNotRemove_LibraryWithoutHeaderConsideredAlwayslinkDependency) {
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
  deps = [
    ":foo",
    ":bar",
  ]
)
)");

  DWYUTestFixture tester(pp.project());
  tester.AddSource("some/path/baz.cc", "/* no include */");
  tester.RunForTarget("//some/path:baz");
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

  DWYUTestFixture tester(pp.project());
  tester.AddSource("some/path/bar.cc", R"(
#include "some/path/baz.pb.h"
)");
  tester.ExpectAdd(":foo_proto_lib");
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

  DWYUTestFixture tester(pp.project());
  tester.AddSource("some/path/bar.cc", "/* no include */");
  tester.ExpectRemove(":foo_proto_lib");
  tester.RunForTarget("//some/path:bar");
}

// In absl/strings:string_view, there is the string_view.h exported.
// But it is _also_ exported by absl/strings:strings but with the remark
// that this is only there for backward compatibility. In fact, it is
// mentioned twice, in hdrs and in textual_hdrs.
// We use this fact for bant to correctly suggest the :string_view
// library. Below, situation re-created.
TEST(DWYUTest, Add_AbslStringViewWorkaround) {
  ParsedProjectTestUtil pp;
  pp.Add("//absl/strings", R"(
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
    DWYUTestFixture tester(pp.project());
    tester.ExpectAdd(":strings");
    tester.ExpectAdd(":string_view");
    tester.AddSource("absl/strings/string-user.cc", R"(
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
)");
    tester.RunForTarget("//absl/strings:string-user");
  }
}

}  // namespace bant
