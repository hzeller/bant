// bant - Bazel Navigation Tool
// Copyright (C) 2025 Henner Zeller <h.zeller@acm.org>
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

#include "bant/util/filesystem.h"

#include "bant/util/file-test-util.h"
#include "gtest/gtest.h"

namespace bant {
TEST(FilesystemTest, DirectoryListing) {
  test::ChangeToTmpDir dir("DirectoryListing");
  dir.touch(".", "baz");
  dir.touch(".", "zulu");
  dir.touch("bar", "abc");
  dir.touch(".", "foo");

  Filesystem &fs = Filesystem::instance();
  const auto &entries = fs.ReadDir(".");
  EXPECT_EQ(entries.size(), 4u);

  // We expect these to be sorted alphabetically.
  EXPECT_EQ(entries[0]->name_as_stringview(), "bar");
  EXPECT_EQ(entries[0]->type, DirectoryEntry::Type::kDirectory);

  EXPECT_EQ(entries[1]->name_as_stringview(), "baz");
  EXPECT_EQ(entries[2]->name_as_stringview(), "foo");
  EXPECT_EQ(entries[3]->name_as_stringview(), "zulu");

  EXPECT_EQ(fs.cache_size(), 1u);

  // Reading dir "./" should not result in a different cache key than "."
  fs.ReadDir("./");
  EXPECT_EQ(fs.cache_size(), 1u);

  EXPECT_TRUE(fs.Exists("bar"));
  EXPECT_TRUE(fs.Exists("./bar"));
  EXPECT_TRUE(fs.Exists("baz"));
  EXPECT_TRUE(fs.Exists("./baz"));

  // Make sure we don't implicitly rely on nul-terminated strings
  const std::string_view substring_checking("foobar");
  EXPECT_FALSE(fs.Exists(substring_checking));  // that is not there.
  EXPECT_TRUE(fs.Exists(substring_checking.substr(0, 3)));  // query "foo"

  EXPECT_EQ(fs.cache_size(), 1u);  // No new directories should be cached yet.

  // Requesting the existence of an item in a subdirectory will read and cache
  // that subdirectory.
  EXPECT_TRUE(fs.Exists("bar/abc"));
  EXPECT_TRUE(fs.Exists("./bar/abc"));
  EXPECT_FALSE(fs.Exists("./bar/xyz"));

  EXPECT_EQ(fs.cache_size(), 2u);

  // Requesting already cached directory with slightly different name: same
  // cache key, so no new entries should make it into the cache.
  fs.ReadDir("bar");
  fs.ReadDir("bar/");
  fs.ReadDir("bar//");
  EXPECT_EQ(fs.cache_size(), 2u);
}
}  // namespace bant
