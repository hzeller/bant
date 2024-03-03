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

#ifndef BANT_FILE_UTILS_H
#define BANT_FILE_UTILS_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace bant {

// This is a replacement for std::filesyste::path which seems to do a lot
// of expensive operations on filenames; sometimes occoupied > 20% bant runtime.
// This, instead, is just a simple wrapper around a string.
class FilesystemPath {
 public:
  FilesystemPath() = default;
  explicit FilesystemPath(std::string_view path) : path_(path) {}

  FilesystemPath(FilesystemPath &&) = default;
  FilesystemPath(const FilesystemPath &) = delete;

  const std::string &path() const { return path_; }

  // Operating system functions might need a nul-terminated string.
  const char *c_str() const { return path_.c_str(); }

  // Just the element after the last slash.
  std::string_view filename() const;

  // Some predicates we use.
  bool is_directory() const;
  bool is_symlink() const;

 private:
  std::string path_;
};

// Given a filename, read the content of the file into a string. If there was
// an error, return a nullopt.
std::optional<std::string> ReadFileToString(const FilesystemPath &filename);

// Collect files found recursively and store in "paths".
// Uses predicate "want_dir_p" to check if directory should be entered, and
// "want_file_p" if file should be included; if so, it is added to "paths".
//
// Returns number of files looked at.
size_t CollectFilesRecursive(
  const FilesystemPath &dir, std::vector<FilesystemPath> &paths,
  const std::function<bool(const FilesystemPath &)> &want_dir_p,
  const std::function<bool(const FilesystemPath &)> &want_file_p);
}  // namespace bant

#endif  // BANT_FILE_UTILS_H
