// bant - Bazel Navigation Tool
// Copyright (C) 2026 Henner Zeller <h.zeller@acm.org>
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

#include "bant/util/hyperlink-builder.h"

#include <sstream>
#include <string>

#include "bant/frontend/source-locator.h"
#include "bant/workspace.h"
#include "gtest/gtest.h"

namespace bant {
namespace {

// We use </a> as closing hyperlink as that is easier to read for the test.
// In typical situations, we would just use the close link
static std::string PrintHyperlinked(const HyperlinkBuilder &h,
                                    const FileLocation &loc) {
  std::stringstream out;
  out << HyperLinked{
    .link_builder = &h, .location = loc, .anchor_close = "</a>"};
  return out.str();
}

TEST(HyperlinkBuilder, SimpleLinkBuilding) {
  BazelWorkspace workspace;
  workspace.external_dir = "my_external_prefix/";
  HyperlinkBuilder b(workspace);
  EXPECT_TRUE(b.Build(
    {
      {"repo_url", "https://repo.com/user/project"},
      {"project_dir", "/abs/project/dir"},
      {"external_dir", "/abs/external/dir"},
    },
    "<a href='", "'>"));

  FileLocation loc;
  loc.line_column_range = {
    .start =
      {
        .line = 42,
        .col = 10,
      },
    .end =
      {
        .line = 45,
        .col = 7,
      },
  };

  loc.filename = "foo/bar/baz.cc";
  EXPECT_EQ(PrintHyperlinked(b, loc),
            "<a href='https://repo.com/user/project/foo/bar/baz.cc#L43-L46'>"
            "foo/bar/baz.cc:43:11:46:7:</a>");

  loc.filename = "my_external_prefix/some-project/foo.h";
  EXPECT_EQ(
    PrintHyperlinked(b, loc),
    "<a href='file:///abs/external/dir/some-project/foo.h?line=43&column=11'>"
    "my_external_prefix/some-project/foo.h:43:11:46:7:</a>");

  loc.filename = "bazel-bin/some/generated.cc";
  EXPECT_EQ(PrintHyperlinked(b, loc),
            "<a "
            "href='file:///abs/project/dir/bazel-bin/some/"
            "generated.cc?line=43&column=11'>"
            "bazel-bin/some/generated.cc:43:11:46:7:</a>");
}

}  // namespace
}  // namespace bant
