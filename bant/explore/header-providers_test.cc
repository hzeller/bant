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

#include "bant/explore/header-providers.h"

#include <sstream>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "bant/frontend/parsed-project_testutil.h"
#include "bant/types-bazel.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Pair;

namespace bant {

// Convenience function to create a BazelTarget for testing.
static BazelTarget T(std::string_view s) {
  auto target_or = BazelTarget::ParseFrom(s, BazelPackage());
  CHECK(target_or.has_value());
  return *target_or;
}

// Same, but return as a set.
static ProvidedFromTargetSet::mapped_type Ts(std::string_view s) {
  auto target_or = BazelTarget::ParseFrom(s, BazelPackage());
  CHECK(target_or.has_value());
  return {*target_or};
}

TEST(HeaderToLibMapping, CCRuleExtraction) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  hdrs = ["foo.h"]
)
)");
  pp.Add("//other/path", R"(
cc_library(
  name = "bar",
  hdrs = ["bar.h"]
)
")");
  pp.Add("//prefix/dir", R"(
cc_library(
  name = "foo",
  hdrs = ["foo.h"],
  include_prefix = "yolo"            # Lib says where header actually is
)
cc_library(
  name = "bar",
  hdrs = ["bar.h"],
  strip_include_prefix = "prefix",   # Remove prefix from current package
)
cc_library(
  name = "baz",
  hdrs = ["subdir/baz.h"],
  includes =[                        # allow to -I without that subdir
     "prefix/dir/subdir",
     "prefix/dir/",                  # trailing slash should not trip
     # TODO: should it also works as "subdir" with package as implicit prefix?
  ]
],
)
)");
  std::stringstream log_absorb;
  auto header_map = ExtractHeaderToLibMapping(pp.project(), log_absorb);
  EXPECT_THAT(header_map,
              Contains(Pair("some/path/foo.h", Ts("//some/path:foo"))));

  EXPECT_THAT(header_map,
              Contains(Pair("other/path/bar.h", Ts("//other/path:bar"))));

  EXPECT_THAT(header_map, Contains(Pair("yolo/foo.h", Ts("//prefix/dir:foo"))));
  EXPECT_THAT(header_map, Contains(Pair("dir/bar.h", Ts("//prefix/dir:bar"))));

  // The header with includes = [...] is available via multiple possible paths.
  EXPECT_THAT(header_map, Contains(Pair("baz.h", Ts("//prefix/dir:baz"))));
  EXPECT_THAT(header_map,
              Contains(Pair("subdir/baz.h", Ts("//prefix/dir:baz"))));
  EXPECT_THAT(header_map, Contains(Pair("prefix/dir/subdir/baz.h",
                                        Ts("//prefix/dir:baz"))));
}

// Sources are much simpler. Just matter-of-fact, no include path fiddling.
TEST(SourceToLibMapping, CCRuleExtraction) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc", "bar.cc"],
  hdrs = ["foo.h"]
)
)");
  std::stringstream log_absorb;
  auto srcs_map = ExtractSourceToLibMapping(pp.project(), log_absorb);
  EXPECT_THAT(srcs_map,
              Contains(Pair("some/path/foo.cc", Ts("//some/path:foo"))));
  EXPECT_THAT(srcs_map,
              Contains(Pair("some/path/bar.cc", Ts("//some/path:foo"))));
}

TEST(HeaderToLibMapping, InludePathsAreRelativePathCanonicalized) {
  ParsedProjectTestUtil pp;
  pp.Add("//", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  include_prefix = ".",
  hdrs = ["foo.h"]
)

cc_library(
  name = "bar",
  srcs = ["bar.cc"],
  include_prefix = "./",
  hdrs = ["bar.h"]
)
)");

  std::stringstream log_absorb;
  auto header_map = ExtractHeaderToLibMapping(pp.project(), log_absorb);
  EXPECT_THAT(header_map, Contains(Pair("foo.h", Ts("//:foo"))));
  EXPECT_THAT(header_map, Contains(Pair("bar.h", Ts("//:bar"))));
}

// Sometimes two different libraries claim to export the same header.
TEST(HeaderToLibMapping, MultipleCCLibsProvideSameHeader) {
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
  hdrs = ["foo.h"]
)
)");
  std::stringstream log_absorb;
  auto header_map = ExtractHeaderToLibMapping(pp.project(), log_absorb);
  const ProvidedFromTargetSet::mapped_type expected_set{T("//some/path:foo"),
                                                        T("//some/path:bar")};
  EXPECT_THAT(header_map, Contains(Pair("some/path/foo.h", expected_set)));
}

TEST(HeaderToLibMapping, ProtoLibraryExtraction) {
  ParsedProjectTestUtil pp;
  pp.Add("//ptest", R"(
proto_library(
   name = "all_protos",
   srcs = [                   # derived from these are the header names ...
      "data.proto",
      ":general.proto",       # also prefix ':' should work.
   ]
)
cc_proto_library(
  name = "foo",               # ... and this is the cc_library it shows up as.
  deps = [":all_protos"],
)

cc_grpc_library(              # GRPC form of a proto library.
  name = "grpc_foo",
  srcs = [":all_protos"],
)
)");
  std::stringstream log_absorb;
  auto header_map = ExtractHeaderToLibMapping(pp.project(), log_absorb);
  EXPECT_THAT(header_map, Contains(Pair("ptest/data.pb.h", Ts("//ptest:foo"))));
  EXPECT_THAT(header_map,
              Contains(Pair("ptest/general.pb.h", Ts("//ptest:foo"))));
  // Another possible suffix.
  EXPECT_THAT(header_map,
              Contains(Pair("ptest/general.proto.h", Ts("//ptest:foo"))));

  // grpc header references
  EXPECT_THAT(header_map,
              Contains(Pair("ptest/data.grpc.pb.h", Ts("//ptest:grpc_foo"))));
  EXPECT_THAT(header_map, Contains(Pair("ptest/general.grpc.pb.h",
                                        Ts("//ptest:grpc_foo"))));
}

TEST(HeaderToLibMapping, GenruleExtraction) {
  ParsedProjectTestUtil pp;
  pp.Add("//gen/ai", R"(
genrule(
  name = "llm",
  outs = [
    "useful.txt",
    "hallucination.txt",
    "lucy-🌈-💎.txt",         # jeez, that escalated quickly.
  ],
)");
  std::stringstream log_absorb;
  auto gen_map = ExtractGeneratedFromGenrule(pp.project(), log_absorb);
  EXPECT_THAT(gen_map, Contains(Pair("gen/ai/useful.txt", T("//gen/ai:llm"))));
  EXPECT_THAT(gen_map,
              Contains(Pair("gen/ai/hallucination.txt", T("//gen/ai:llm"))));
  EXPECT_THAT(gen_map,
              Contains(Pair("gen/ai/lucy-🌈-💎.txt", T("//gen/ai:llm"))));
}

static std::string reverse(std::string_view in) {
  // This is a whitebox approach as we know how the index needs to be stored,
  // but ok as header-providers.cc and this test go hand-in-hand.
  return std::string{in.rbegin(), in.rend()}.append("/");
}

// Check for existence of value of fail if not.
#define ASSERT_HAS_VALUE(x)     \
  ({                            \
    auto v = (x);               \
    CHECK(v.has_value()) << #x; \
    v.value();                  \
  })

TEST(HeaderProviders, FindBySuffixTest) {
  ProvidedFromTargetSet test_index;
  test_index[reverse("foo/bar/baz/qux.h")].insert(T("//foo"));
  test_index[reverse("baz/qux.h")].insert(T("//bar"));

  // Exact match should only return that element
  FindResult result =
    ASSERT_HAS_VALUE(FindBySuffix(test_index, "foo/bar/baz/qux.h"));
  EXPECT_EQ(result.fuzzy_score, 0);  // no fuzzy, full match.
  EXPECT_THAT(*result.target_set, ElementsAre(T("//foo")));

  result = ASSERT_HAS_VALUE(FindBySuffix(test_index, "baz/qux.h"));
  EXPECT_EQ(result.fuzzy_score, 0);  // no fuzzy, full match.
  EXPECT_THAT(*result.target_set, ElementsAre(T("//bar")));

  // Below, only fuzzy matches happen.

  // fuzzy matches with different amount of expected path elements
  EXPECT_FALSE(FindBySuffix(test_index, "qux.h", 2).has_value());  // 2 slash
  EXPECT_TRUE(FindBySuffix(test_index, "qux.h", 1).has_value());   // 1 slash
  EXPECT_FALSE(FindBySuffix(test_index, "ux.h", 1).has_value());   // not even 1

  // so in general shorter matches than to the first slash don't count.
  EXPECT_FALSE(FindBySuffix(test_index, "bar/xqux.h", 1).has_value());

  // Other fuzzy matches should return the candiate matching closest.
  result = ASSERT_HAS_VALUE(FindBySuffix(test_index, "az/qux.h", 1));
  EXPECT_EQ(result.fuzzy_score, 1);
  EXPECT_THAT(*result.target_set, ElementsAre(T("//bar")));

  result = ASSERT_HAS_VALUE(FindBySuffix(test_index, "r/baz/qux.h"));
  EXPECT_EQ(result.fuzzy_score, 2);
  EXPECT_THAT(*result.target_set, ElementsAre(T("//foo")));

  result = ASSERT_HAS_VALUE(FindBySuffix(test_index, "bar/baz/qux.h"));
  EXPECT_EQ(result.fuzzy_score, 3);
  EXPECT_THAT(*result.target_set, ElementsAre(T("//foo")));

  // Longer path than is in index, but with the same suffix. It will
  // be one before the match. We find it before end()
  result =
    ASSERT_HAS_VALUE(FindBySuffix(test_index, "hello/foo/bar/baz/qux.h"));
  EXPECT_EQ(result.fuzzy_score, 4);
  EXPECT_THAT(*result.target_set, ElementsAre(T("//foo")));

  // If there is another entry afterwards, we also find it.
  test_index[reverse("foo/bar/baz/rux.h")].insert(T("//rux"));
  result =
    ASSERT_HAS_VALUE(FindBySuffix(test_index, "hello/foo/bar/baz/qux.h"));
  EXPECT_EQ(result.fuzzy_score, 4);
  EXPECT_THAT(*result.target_set, ElementsAre(T("//foo")));
}

// Needs test:
// strip_import_prefix
// aliases for proto libraries.
}  // namespace bant
