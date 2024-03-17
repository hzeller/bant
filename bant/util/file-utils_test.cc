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

#include "bant/util/file-utils.h"

#include <string.h>

#include "gtest/gtest.h"

namespace bant {
TEST(FileUtils, FilesystemPathFromDirent) {
  struct dirent entry {};
  strncpy(entry.d_name, "baz", sizeof(entry.d_name));
  entry.d_type = DT_DIR;
  FilesystemPath from_dirent("foo/bar///", entry);

  EXPECT_EQ(from_dirent.path(), "foo/bar/baz");  // multi-slash removed
  EXPECT_EQ(from_dirent.filename(), "baz");

  // We're now quering the file properties. Since the path above certainly
  // does not exist and can not be stat()-ed we know that we get is from the
  // memoization passed in from the entry.d_type
  EXPECT_TRUE(from_dirent.is_directory());
  EXPECT_FALSE(from_dirent.is_symlink());
}

TEST(FileUtils, FilesystemPathFromPath) {
  FilesystemPath from_path("foo/bar/baz");
  EXPECT_EQ(from_path.path(), "foo/bar/baz");
  EXPECT_EQ(from_path.filename(), "baz");
}

TEST(FileUtils, FilesystemPathCopy) {
  FilesystemPath from_path("foo/bar/baz");
  EXPECT_EQ(from_path.path(), "foo/bar/baz");
  EXPECT_EQ(from_path.filename(), "baz");

  // Make sure a copied filename (that has a different path() string location,
  // still outputs the correct filename (and does not have it cached as
  // string-view pointing to the wrong location).
  FilesystemPath other = from_path;
  EXPECT_NE(&from_path.path(), &other.path());
  EXPECT_EQ(other.filename(), "baz");
}

}  // namespace bant
