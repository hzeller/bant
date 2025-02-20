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

#include "bant/types-bazel.h"

#include <optional>
#include <string_view>

#include "absl/log/check.h"
#include "gtest/gtest.h"

namespace bant {

// Convenience methods to parse packages and patterns where we know the parse
// should be successful.

static BazelPackage PackageOrDie(std::string_view s) {
  std::optional<BazelPackage> package_or = BazelPackage::ParseFrom(s);
  CHECK(package_or.has_value()) << s;
  return package_or.value();
}

static BazelTarget TargetOrDie(std::string_view s,
                               const BazelPackage &context) {
  std::optional<BazelTarget> target_or = BazelTarget::ParseFrom(s, context);
  CHECK(target_or.has_value()) << s << "(relative to " << context << ")";
  return target_or.value();
}
static BazelTarget TargetOrDie(std::string_view s) {
  return TargetOrDie(s, BazelPackage("", ""));
}

static BazelPattern PatternOrDie(std::string_view s) {
  std::optional<BazelPattern> pattern_or = BazelPattern::ParseFrom(s);
  CHECK(pattern_or.has_value()) << s;
  return pattern_or.value();
}

static BazelPattern VisibilityOrDie(std::string_view s,
                                    const BazelPackage &context) {
  std::optional<BazelPattern> visibility_or =
    BazelPattern::ParseVisibility(s, context);
  CHECK(visibility_or.has_value()) << s;
  return visibility_or.value();
}

TEST(TypesBazel, ParsePackage) {
  {
    const BazelPackage p = PackageOrDie("nodelimiter");
    EXPECT_EQ(p.project, "");
    EXPECT_EQ(p.path, "nodelimiter");
  }

  {
    const BazelPackage p = PackageOrDie("@foo");
    EXPECT_EQ(p.project, "@foo");
    EXPECT_TRUE(p.path.empty());
  }

  {
    const BazelPackage p = PackageOrDie("//foo/bar");
    EXPECT_TRUE(p.project.empty());
    EXPECT_EQ(p.path, "foo/bar");
  }

  {  // Trailing slash removed.
    const BazelPackage p = PackageOrDie("//foo/bar/");
    EXPECT_TRUE(p.project.empty());
    EXPECT_EQ(p.path, "foo/bar");
  }

  {
    const BazelPackage p = PackageOrDie("//foo/bar:targetignored");
    EXPECT_TRUE(p.project.empty());
    EXPECT_EQ(p.path, "foo/bar");
  }

  {
    const BazelPackage p = PackageOrDie("@foo//bar/baz");
    EXPECT_EQ(p.project, "@foo");
    EXPECT_EQ(p.path, "bar/baz");
  }

  // Some not quite proper formatted
  {
    const BazelPackage p = PackageOrDie("@foo/bar/baz");
    EXPECT_EQ(p.project, "@foo");
    EXPECT_EQ(p.path, "bar/baz");
  }

  // ... but double slashes at the wrong place goes too far
  {
    auto p = BazelPackage::ParseFrom("@foo/bar//baz");
    ASSERT_FALSE(p.has_value());
    auto q = BazelPackage::ParseFrom("@foo/bar/baz//abc");
    ASSERT_FALSE(q.has_value());
  }

  // Empty project is just the regular project
  {
    const BazelPackage p = PackageOrDie("@//bar/baz");
    EXPECT_EQ(p.project, "");
    EXPECT_EQ(p.path, "bar/baz");
  }
}

TEST(TypesBazel, PrintPackage) {
  {
    const BazelPackage p("", "foo/bar/baz");
    EXPECT_EQ(p.ToString(), "//foo/bar/baz");
  }
  {
    const BazelPackage p("@absl", "foo/bar/baz");
    EXPECT_EQ(p.ToString(), "@absl//foo/bar/baz");
  }
  {
    const BazelPackage p("@foo", "");
    EXPECT_EQ(p.ToString(), "@foo//");
  }
}

TEST(TypesBazel, ParseTarget) {
  const BazelPackage context("", "foo/bar");
  {
    const BazelTarget t = TargetOrDie(":target", context);
    EXPECT_EQ(t.package, context);
    EXPECT_EQ(t.target_name, "target");
  }

  {
    // Not well-formed, but we'll still parse it.
    const BazelTarget t = TargetOrDie("target", context);
    EXPECT_EQ(t.package, context);
    EXPECT_EQ(t.target_name, "target");
  }

  {
    const BazelTarget t = TargetOrDie("//baz", context);
    EXPECT_EQ(t.package.path, "baz");
    EXPECT_EQ(t.target_name, "baz");
  }

  {
    const BazelTarget t = TargetOrDie("//baz/", context);
    EXPECT_EQ(t.package.path, "baz");
    EXPECT_EQ(t.target_name, "");  // or should this also be "baz" ?
  }

  {
    const BazelTarget t = TargetOrDie("@foo", context);
    EXPECT_EQ(t.package, BazelPackage("@foo", ""));
    EXPECT_EQ(t.target_name, "foo");
  }

  {
    const BazelTarget t = TargetOrDie("//other/path:target", context);
    EXPECT_EQ(t.package, BazelPackage("", "other/path"));
    EXPECT_EQ(t.target_name, "target");
  }

  {
    const BazelTarget t = TargetOrDie("//some/path/toplevel", context);
    EXPECT_EQ(t.package, BazelPackage("", "some/path/toplevel"));
    EXPECT_EQ(t.target_name, "toplevel");
  }

  for (const std::string_view test_case :
       {"@absl//absl/strings:strings", "@absl//absl/strings"}) {
    const BazelTarget t = TargetOrDie(test_case, context);
    EXPECT_EQ(t.package, BazelPackage("@absl", "absl/strings"));
    EXPECT_EQ(t.target_name, "strings");
  }

  const BazelPackage project_context("@absl", "foo/bar");
  for (const std::string_view test_case :
       {"//absl/strings:strings", "//absl/strings"}) {
    const BazelTarget t = TargetOrDie(test_case, project_context);
    EXPECT_EQ(t.package, BazelPackage("@absl", "absl/strings"));
    EXPECT_EQ(t.target_name, "strings");
  }
}

TEST(TypesBazel, QualifiedFile) {
  const BazelPackage p("", "bar/baz");
  EXPECT_EQ(p.QualifiedFile("quux.cc"), "bar/baz/quux.cc");
  EXPECT_EQ(p.QualifiedFile(":quux.cc"), "bar/baz/quux.cc");
}

TEST(TypesBazel, PrintTarget) {
  const BazelPackage p1("", "foo/bar/baz");
  const BazelPackage p2("", "other/path");

  const BazelTarget tlib = TargetOrDie("some-lib", p1);
  EXPECT_EQ(tlib.ToString(), "//foo/bar/baz:some-lib");
  EXPECT_EQ(tlib.ToStringRelativeTo(p1), ":some-lib");
  EXPECT_EQ(tlib.ToStringRelativeTo(p2), "//foo/bar/baz:some-lib");

  const BazelTarget baz = TargetOrDie("baz", p1);
  EXPECT_EQ(baz.ToString(), "//foo/bar/baz");
  EXPECT_EQ(baz.ToStringRelativeTo(p1), ":baz");
  EXPECT_EQ(baz.ToStringRelativeTo(p2), "//foo/bar/baz");

  const BazelPackage pack("@project", "");
  const BazelTarget pack_t1 = TargetOrDie("foo", pack);
  EXPECT_EQ(pack_t1.ToString(), "@project//:foo");
  EXPECT_EQ(pack_t1.ToStringRelativeTo(pack), ":foo");

  // Toplevel tareget same as project
  const BazelTarget pack_t2 = TargetOrDie("project", pack);
  EXPECT_EQ(pack_t2.ToString(), "@project");
  EXPECT_EQ(pack_t2.ToStringRelativeTo(pack), ":project");
}

// Quick tests.
TEST(TypesBazel, ParseRePrint) {
  const BazelPackage c("", "foo");

  EXPECT_EQ("//foo/bar:baz", TargetOrDie("//foo/bar:baz", c).ToString());
  EXPECT_EQ("//foo", TargetOrDie("//foo", c).ToString());
  EXPECT_EQ("//foo", TargetOrDie("//foo:foo", c).ToString());
  EXPECT_EQ("@foo//:baz", TargetOrDie("@foo//:baz", c).ToString());
  EXPECT_EQ("@foo//foo", TargetOrDie("@foo//foo", c).ToString());
  EXPECT_EQ("@foo", TargetOrDie("@foo//:foo", c).ToString());

  EXPECT_EQ("//bar", TargetOrDie("//bar", c).ToString());
  EXPECT_EQ("//bar", TargetOrDie("//bar:bar", c).ToString());

  EXPECT_EQ("@foo//bar", TargetOrDie("@foo//bar", c).ToString());
  EXPECT_EQ("@foo//bar", TargetOrDie("@foo//bar:bar", c).ToString());
}

TEST(TypesBazel, InvalidBazelPatterns) {
  // TODO: capture message.
  EXPECT_FALSE(BazelPattern::ParseFrom("foo/bar/").has_value());
  EXPECT_FALSE(BazelPattern::ParseFrom("foo/bar:").has_value());
}

TEST(TypesBazel, CheckRecursivePatterns) {
  EXPECT_TRUE(PatternOrDie("//...").is_recursive());
  EXPECT_TRUE(PatternOrDie("...").is_recursive());
  EXPECT_TRUE(PatternOrDie("foo/bar/...").is_recursive());
  EXPECT_TRUE(PatternOrDie("//foo/bar/...").is_recursive());

  // Typo, so regular non-recursive pattern matching.
  EXPECT_FALSE(PatternOrDie("foo/bar/..").is_recursive());

  EXPECT_FALSE(PatternOrDie("foo/bar:all").is_recursive());
  EXPECT_FALSE(PatternOrDie("foo/bar:__pkg__").is_recursive());
  EXPECT_TRUE(PatternOrDie("foo/bar:__subpackages__").is_recursive());
}

TEST(TypesBazel, CheckPatternPaths) {
  EXPECT_EQ(PatternOrDie("//...").path(), "");
  EXPECT_EQ(PatternOrDie("...").path(), "");
  EXPECT_EQ(PatternOrDie("//foo/bar/...").path(), "foo/bar");
  EXPECT_EQ(PatternOrDie("foo/bar/...").path(), "foo/bar");
  EXPECT_EQ(PatternOrDie("foo/bar:all").path(), "foo/bar");
  EXPECT_EQ(PatternOrDie("foo/bar:__pkg__").path(), "foo/bar");
  EXPECT_EQ(PatternOrDie("foo/bar:__subpackages__").path(), "foo/bar");
}

TEST(TypesBazel, CheckPatternPackageMatch) {
  EXPECT_TRUE(PatternOrDie("...").Match(PackageOrDie("//foo")));
  EXPECT_TRUE(PatternOrDie("...").Match(PackageOrDie("//foo:bar")));
  EXPECT_TRUE(PatternOrDie("...").Match(PackageOrDie("//foo/bar:baz")));
  EXPECT_FALSE(PatternOrDie("...").Match(PackageOrDie("@quux//foo/bar:baz")));

  EXPECT_TRUE(PatternOrDie("//...").Match(PackageOrDie("//foo:bar")));

  EXPECT_TRUE(PatternOrDie("//foo/...").Match(PackageOrDie("//foo")));
  EXPECT_TRUE(PatternOrDie("//foo/...").Match(PackageOrDie("//foo/bar")));
  EXPECT_FALSE(PatternOrDie("//foo/...").Match(PackageOrDie("//foobar")));

  EXPECT_TRUE(
    PatternOrDie("//foo:__subpackages__").Match(PackageOrDie("//foo/bar")));
  EXPECT_FALSE(
    PatternOrDie("//foo:__subpackages__").Match(PackageOrDie("//baz")));

  EXPECT_FALSE(PatternOrDie("@x//foo/...").Match(PackageOrDie("//foo")));
  EXPECT_FALSE(PatternOrDie("//foo/...").Match(PackageOrDie("@x//foo")));

  EXPECT_TRUE(PatternOrDie("//foo:all").Match(PackageOrDie("//foo")));
  EXPECT_FALSE(PatternOrDie("//foo:all").Match(PackageOrDie("//foo/bar")));
}

TEST(TypesBazel, CheckPatternTargetMatch) {
  EXPECT_TRUE(PatternOrDie("//foo/...").Match(TargetOrDie("//foo:bar")));
  EXPECT_FALSE(PatternOrDie("//foo/...").Match(TargetOrDie("@foo//foo:bar")));
  EXPECT_TRUE(PatternOrDie("//foo/...").Match(TargetOrDie("//foo/bar:baz")));

  EXPECT_TRUE(PatternOrDie("//foo/...").Match(TargetOrDie("//foo")));
  EXPECT_FALSE(PatternOrDie("//foo/...").Match(TargetOrDie("//fo")));

  EXPECT_TRUE(PatternOrDie("//foo").Match(TargetOrDie("//foo")));
  EXPECT_TRUE(PatternOrDie("//foo/...").Match(TargetOrDie("//foo/")));

  // All in package match
  EXPECT_TRUE(PatternOrDie("//foo:all").Match(TargetOrDie("//foo:bar")));
  EXPECT_TRUE(PatternOrDie("//foo:all").Match(TargetOrDie("//foo:baz")));
  EXPECT_TRUE(PatternOrDie("//foo:*").Match(TargetOrDie("//foo:baz")));
  EXPECT_TRUE(PatternOrDie("//foo:__pkg__").Match(TargetOrDie("//foo:baz")));

  // Exact match
  EXPECT_TRUE(PatternOrDie("//foo:bar").Match(TargetOrDie("//foo:bar")));
  EXPECT_FALSE(PatternOrDie("//foo:bar").Match(TargetOrDie("//foo:baz")));

  // Globbing match
  EXPECT_TRUE(PatternOrDie("//foo:*bar").Match(TargetOrDie("//foo:bar")));
  EXPECT_FALSE(PatternOrDie("//foo:*bar").Match(TargetOrDie("//foo:baz")));
  EXPECT_TRUE(PatternOrDie("//foo:*bar").Match(TargetOrDie("//foo:foobar")));
  EXPECT_TRUE(PatternOrDie("//foo:*bar").Match(TargetOrDie("//foo:whiskybar")));
  EXPECT_FALSE(PatternOrDie("//foo:*bar").Match(TargetOrDie("//foo:barquux")));

  // At end.
  EXPECT_TRUE(PatternOrDie("//foo:ba*").Match(TargetOrDie("//foo:bar")));
  EXPECT_TRUE(PatternOrDie("//foo:ba*").Match(TargetOrDie("//foo:baz")));
  EXPECT_FALSE(PatternOrDie("//foo:ba*").Match(TargetOrDie("//foo:quux")));

  // Middle.
  EXPECT_TRUE(PatternOrDie("//foo:b*r").Match(TargetOrDie("//foo:bar")));
  EXPECT_TRUE(PatternOrDie("//foo:b*r").Match(TargetOrDie("//foo:bazaar")));
  EXPECT_FALSE(PatternOrDie("//foo:b*r").Match(TargetOrDie("//foo:baz")));

  // Multiple.
  EXPECT_TRUE(PatternOrDie("//foo:b*r*k").Match(TargetOrDie("//foo:break")));
  EXPECT_TRUE(
    PatternOrDie("//foo:b*r*k").Match(TargetOrDie("//foo:be_right_back")));
  EXPECT_FALSE(
    PatternOrDie("//foo:b*r*k").Match(TargetOrDie("//foo:ill_be_back")));

  // Should the following work ?
  // EXPECT_TRUE(PatternOrDie("//foo").Match(TargetOrDie("//foo/")));
}

TEST(TypesBazel, CheckVisibilityTargetMatch) {
  const BazelPackage p = PackageOrDie("//foo/bar");
  // Public essentially means: no visibility filter.
  EXPECT_FALSE(VisibilityOrDie("//visibility:public", p).HasFilter());
  EXPECT_TRUE(VisibilityOrDie("//visibility:private", p).HasFilter());

  // Private means only packages in exactly the context package.
  EXPECT_TRUE(VisibilityOrDie("//visibility:private", p)
                .Match(TargetOrDie("//foo/bar:baz")));
  EXPECT_FALSE(VisibilityOrDie("//visibility:private", p)
                 .Match(TargetOrDie("//foo/bar/baz:quux")));

  EXPECT_TRUE(VisibilityOrDie("__subpackages__", p).HasFilter());
  EXPECT_TRUE(VisibilityOrDie("__subpackages__", p).is_recursive());
  EXPECT_TRUE(VisibilityOrDie("__subpackages__", p)
                .Match(TargetOrDie("//foo/bar:hello")));
  EXPECT_TRUE(VisibilityOrDie("__subpackages__", p)
                .Match(TargetOrDie("//foo/bar/baz/and/deep/belo:hello")));
}
}  // namespace bant
