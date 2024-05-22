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

#include "bant/util/glob-match-builder.h"

#include "gtest/gtest.h"

namespace bant {
TEST(GlobMatcchBuilderTest, NoDirectorySimpleFileGlob) {
  GlobMatchBuilder glob_builder;
  glob_builder.AddIncludePattern("foo.txt");
  glob_builder.AddIncludePattern("b*r.txt");

  auto file_is_matching = glob_builder.BuildFileMatchPredicate();
  EXPECT_TRUE(file_is_matching("foo.txt"));
  EXPECT_FALSE(file_is_matching("fooXtxt"));  // Reall matching dot, not any

  EXPECT_TRUE(file_is_matching("br.txt"));
  EXPECT_TRUE(file_is_matching("bar.txt"));
  EXPECT_TRUE(file_is_matching("baaaaar.txt"));
  EXPECT_FALSE(file_is_matching("car.txt"));

  auto dir_is_matching = glob_builder.BuildDirectoryMatchPredicate();
  EXPECT_TRUE(dir_is_matching(""));
  EXPECT_FALSE(dir_is_matching("anythingelse"));
}

TEST(GlobMatcchBuilderTest, ExactlyOneDir) {
  GlobMatchBuilder glob_builder;
  glob_builder.AddIncludePattern("*/foo.txt");
  glob_builder.AddIncludePattern("*/b*r.txt");

  auto file_is_matching = glob_builder.BuildFileMatchPredicate();
  EXPECT_FALSE(file_is_matching("foo.txt"));
  EXPECT_FALSE(file_is_matching("baaaaar.txt"));
  EXPECT_TRUE(file_is_matching("a/foo.txt"));
  EXPECT_TRUE(file_is_matching("a/bar.txt"));

  auto dir_is_matching = glob_builder.BuildDirectoryMatchPredicate();
  EXPECT_TRUE(dir_is_matching(""));
  EXPECT_TRUE(dir_is_matching("foo"));
  EXPECT_FALSE(dir_is_matching("foo/bar"));
}

TEST(GlobMatcchBuilderTest, MultiDir) {
  GlobMatchBuilder glob_builder;
  glob_builder.AddIncludePattern("**/foo.txt");
  glob_builder.AddIncludePattern("**/b*r.txt");

  auto file_is_matching = glob_builder.BuildFileMatchPredicate();
  EXPECT_FALSE(file_is_matching("foo.txt"));
  EXPECT_FALSE(file_is_matching("baaaaar.txt"));
  EXPECT_TRUE(file_is_matching("a/foo.txt"));
  EXPECT_TRUE(file_is_matching("a/bar.txt"));
  EXPECT_TRUE(file_is_matching("a/b/foo.txt"));
  EXPECT_TRUE(file_is_matching("a/b/c/foo.txt"));

  auto dir_is_matching = glob_builder.BuildDirectoryMatchPredicate();
  EXPECT_TRUE(dir_is_matching(""));
  EXPECT_TRUE(dir_is_matching("foo"));
  EXPECT_TRUE(dir_is_matching("foo/bar"));
  EXPECT_TRUE(dir_is_matching("foo/bar/baz"));
}

TEST(GlobMatcchBuilderTest, MultiDirWithPrefix) {
  GlobMatchBuilder glob_builder;
  glob_builder.AddIncludePattern("a/**/foo.txt");
  glob_builder.AddIncludePattern("b/**/b*r.txt");

  auto file_is_matching = glob_builder.BuildFileMatchPredicate();
  EXPECT_FALSE(file_is_matching("foo.txt"));
  EXPECT_FALSE(file_is_matching("baaaaar.txt"));

  EXPECT_TRUE(file_is_matching("a/x/foo.txt"));
  EXPECT_FALSE(file_is_matching("a/x/bar.txt"));
  EXPECT_TRUE(file_is_matching("b/x/bar.txt"));
  EXPECT_TRUE(file_is_matching("b/x/baaar.txt"));

  EXPECT_TRUE(file_is_matching("a/b/c/d/foo.txt"));
  EXPECT_FALSE(file_is_matching("a/b/c/d/bar.txt"));
  EXPECT_TRUE(file_is_matching("b/c/d/bar.txt"));

  auto dir_is_matching = glob_builder.BuildDirectoryMatchPredicate();
  EXPECT_FALSE(dir_is_matching(""));  // We need to have at least one prefix
  EXPECT_TRUE(dir_is_matching("a"));
  EXPECT_TRUE(dir_is_matching("a/b"));
  EXPECT_TRUE(dir_is_matching("a/b/c"));

  EXPECT_TRUE(dir_is_matching("b"));
  EXPECT_TRUE(dir_is_matching("b/c"));
  EXPECT_TRUE(dir_is_matching("b/c/d"));

  EXPECT_FALSE(dir_is_matching("c"));
}

TEST(GlobMatcchBuilderTest, ExcludeFiles) {
  GlobMatchBuilder glob_builder;
  glob_builder.AddIncludePattern("*.txt");
  glob_builder.AddExcludePattern("*_internal*.txt");

  auto file_is_matching = glob_builder.BuildFileMatchPredicate();
  EXPECT_TRUE(file_is_matching("foo.txt"));
  EXPECT_TRUE(file_is_matching("bar.txt"));
  EXPECT_TRUE(file_is_matching("foo_test.txt"));

  EXPECT_TRUE(file_is_matching("foo_intern.txt"));
  EXPECT_FALSE(file_is_matching("foo_internal.txt"));
  EXPECT_FALSE(file_is_matching("foo_internals.txt"));
}
}  // namespace bant
