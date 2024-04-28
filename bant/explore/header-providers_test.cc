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

#include "bant/explore/header-providers.h"

#include <sstream>
#include <string_view>

#include "absl/log/check.h"
#include "bant/frontend/parsed-project_testutil.h"
#include "bant/types-bazel.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Contains;
using testing::Pair;

namespace bant {

// Convenience function to create a BazelTarget for testing.
static BazelTarget T(std::string_view s) {
  auto target_or = BazelTarget::ParseFrom(s, BazelPackage());
  CHECK(target_or.has_value());
  return *target_or;
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
              Contains(Pair("some/path/foo.h", T("//some/path:foo"))));

  EXPECT_THAT(header_map,
              Contains(Pair("other/path/bar.h", T("//other/path:bar"))));

  EXPECT_THAT(header_map, Contains(Pair("yolo/foo.h", T("//prefix/dir:foo"))));
  EXPECT_THAT(header_map, Contains(Pair("dir/bar.h", T("//prefix/dir:bar"))));

  // The header with includes = [...] is available via multiple possible paths.
  EXPECT_THAT(header_map, Contains(Pair("baz.h", T("//prefix/dir:baz"))));
  EXPECT_THAT(header_map,
              Contains(Pair("subdir/baz.h", T("//prefix/dir:baz"))));
  EXPECT_THAT(header_map,
              Contains(Pair("prefix/dir/subdir/baz.h", T("//prefix/dir:baz"))));
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
)");
  std::stringstream log_absorb;
  auto header_map = ExtractHeaderToLibMapping(pp.project(), log_absorb);
  EXPECT_THAT(header_map, Contains(Pair("ptest/data.pb.h", T("//ptest:foo"))));
  EXPECT_THAT(header_map,
              Contains(Pair("ptest/general.pb.h", T("//ptest:foo"))));
}

TEST(HeaderToLibMapping, GenruleExtraction) {
  ParsedProjectTestUtil pp;
  pp.Add("//gen/ai", R"(
genrule(
  name = "llm",
  outs = [
    "useful.txt",
    "hallucination.txt",
    "lucy-🌈-💎.txt",         # jeez, going wild.
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

}  // namespace bant
