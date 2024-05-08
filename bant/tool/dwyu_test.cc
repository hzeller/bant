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
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "bant/frontend/linecolumn-map.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/parsed-project_testutil.h"
#include "bant/output-format.h"
#include "bant/session.h"
#include "bant/tool/dwyu-internal.h"
#include "bant/tool/edit-callback_testutil.h"
#include "bant/types-bazel.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::ElementsAre;

namespace bant {

static LineColumn PosOfPart(const NamedLineIndexedContent &src,
                            const std::vector<std::string_view> &parts,
                            size_t i) {
  CHECK(i <= parts.size());
  return src.GetRange(parts[i]).start;
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
)";
  NamedLineIndexedContent scanned_src("<text>", kTestContent);
  const auto includes = ExtractCCIncludes(&scanned_src);
  EXPECT_THAT(includes, ElementsAre("CaSe-dash_underscore.h", "but-this.h",
                                    "with/suffix.hh", "with/suffix.pb.h",
                                    "with/suffix.inc", "w/space.h"));
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
      : session_(MkSession()),
        dwyu_(session_, project, edit_expector_.checker()){};

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

 private:
  static Session MkSession() {
    return {&std::cerr, &std::cerr, true, OutputFormat::kNative};
  }

  Session session_;
  EditExpector edit_expector_;
  TestableDWYUGenerator dwyu_;
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
  srcs = ["bar.cc"],
  # needed :foo dependency not given
)
)");

  {
    DWYUTestFixture tester(pp.project());
    tester.ExpectAdd(":foo");
    tester.AddSource("some/path/bar.cc", R"(
#include "some/path/foo.h"
)");
    tester.RunForTarget("//some/path:bar");
  }

  {  // Files relative to current directory are properly handled.
    DWYUTestFixture tester(pp.project());
    tester.ExpectAdd(":foo");
    tester.AddSource("some/path/bar.cc", R"(
#include "foo.h"
)");
    tester.RunForTarget("//some/path:bar");
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
#include "some/path/some-unaccounted-header.h"
)");
  tester.RunForTarget("//some/path:bar");
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
  deps = [":foo"],
)
)");

  DWYUTestFixture tester(pp.project());
  tester.AddSource("some/path/bar.cc", "/* no include */");
  tester.RunForTarget("//some/path:bar");
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
}  // namespace bant
