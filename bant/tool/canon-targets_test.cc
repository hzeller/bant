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

#include "bant/tool/canon-targets.h"

#include <iostream>

#include "bant/frontend/parsed-project.h"
#include "bant/frontend/parsed-project_testutil.h"
#include "bant/session.h"
#include "bant/tool/edit-callback_testutil.h"
#include "bant/types-bazel.h"
#include "gtest/gtest.h"

namespace bant {

TEST(CanoniazlieTest, CanonicalizeDependencies) {
  ParsedProjectTestUtil pp;
  pp.Add("//some/path", R"(
cc_library(
  name = "foo",
  srcs = ["foo.cc"],
  deps = [
    "//some/path:bar",        # local package, so keep local :bar
    "baz",                    # add colon prefix
    "//flubber:flubber",      # can be shortened to //flubber
    "//other/package:waldo",  # should not change
    "@//other/package:qux",   # This has a superfluous '@' in front.
    "@foobar//:foobar",       # can be shortended to @foobar
  ]
)
)");

  Session session(&std::cerr, &std::cerr, CommandlineFlags{.verbose = 1});
  EditExpector edit_expector;
  edit_expector.ExpectRename("baz", ":baz");
  edit_expector.ExpectRename("//some/path:bar", ":bar");
  edit_expector.ExpectRename("//flubber:flubber", "//flubber");
  edit_expector.ExpectRename("@//other/package:qux", "//other/package:qux");
  edit_expector.ExpectRename("@foobar//:foobar", "@foobar");
  CreateCanonicalizeEdits(session, pp.project(), BazelPattern(),
                          edit_expector.checker());
}

}  // namespace bant
