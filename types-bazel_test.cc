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

#include "types-bazel.h"

#include "gtest/gtest.h"

namespace bant {
TEST(TypesBazel, ParsePackage) {
  {
    auto p = BazelPackage::ParseFrom("nodelimiter");
    EXPECT_FALSE(p.has_value());
  }

  {
    auto p = BazelPackage::ParseFrom("@foo");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->project, "@foo");
    EXPECT_TRUE(p->path.empty());
  }

  {
    auto p = BazelPackage::ParseFrom("//foo/bar");
    ASSERT_TRUE(p.has_value());
    EXPECT_TRUE(p->project.empty());
    EXPECT_EQ(p->path, "foo/bar");
  }

  {
    auto p = BazelPackage::ParseFrom("//foo/bar:targetignored");
    ASSERT_TRUE(p.has_value());
    EXPECT_TRUE(p->project.empty());
    EXPECT_EQ(p->path, "foo/bar");
  }
}

TEST(TypesBazel, PrintPackage) {
  {
    BazelPackage p("", "foo/bar/baz");
    EXPECT_EQ(p.ToString(), "//foo/bar/baz");
  }
  {
    BazelPackage p("@absl", "foo/bar/baz");
    EXPECT_EQ(p.ToString(), "@absl//foo/bar/baz");
  }
  {
    BazelPackage p("@foo", "");
    EXPECT_EQ(p.ToString(), "@foo//");
  }
}

TEST(TypesBazel, ParseTarget) {
  BazelPackage context("", "foo/bar");
  {
    auto t = BazelTarget::ParseFrom(":target", context);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->package, context);
    EXPECT_EQ(t->target_name, "target");
  }

  {
    EXPECT_FALSE(BazelTarget::LooksWellformed("target"));

    // Not well-formed, but we'll still parse it.
    auto t = BazelTarget::ParseFrom("target", context);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->package, context);
    EXPECT_EQ(t->target_name, "target");
  }

  {
    auto t = BazelTarget::ParseFrom("@foo", context);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->package, BazelPackage("@foo", ""));
    EXPECT_EQ(t->target_name, "foo");
  }

  {
    auto t = BazelTarget::ParseFrom("//other/path:target", context);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->package, BazelPackage("", "other/path"));
    EXPECT_EQ(t->target_name, "target");
  }

  {
    auto t = BazelTarget::ParseFrom("//some/path/toplevel", context);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->package, BazelPackage("", "some/path/toplevel"));
    EXPECT_EQ(t->target_name, "toplevel");
  }

  for (std::string_view test_case :
       {"@absl//absl/strings:strings", "@absl//absl/strings"}) {
    auto t = BazelTarget::ParseFrom(test_case, context);
    ASSERT_TRUE(t.has_value()) << test_case;
    EXPECT_EQ(t->package, BazelPackage("@absl", "absl/strings"));
    EXPECT_EQ(t->target_name, "strings");
  }
}

TEST(TypesBazel, PrintTarget) {
  BazelPackage p1("", "foo/bar/baz");
  BazelPackage p2("", "other/path");

  BazelTarget tlib(p1, "some-lib");
  EXPECT_EQ(tlib.ToString(), "//foo/bar/baz:some-lib");
  EXPECT_EQ(tlib.ToStringRelativeTo(p1), ":some-lib");
  EXPECT_EQ(tlib.ToStringRelativeTo(p2), "//foo/bar/baz:some-lib");

  BazelTarget baz(p1, "baz");
  EXPECT_EQ(baz.ToString(), "//foo/bar/baz");
  EXPECT_EQ(baz.ToStringRelativeTo(p1), ":baz");
  EXPECT_EQ(baz.ToStringRelativeTo(p2), "//foo/bar/baz");
}
}  // namespace bant
