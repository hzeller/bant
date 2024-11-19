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

#include <dirent.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bant {

// This is a replacement for std::filesyste::path which seems to do a lot
// of expensive operations on filenames; sometimes occoupied > 20% bant runtime.
// This, instead, is mostly a simple wrapper around a string.
class FilesystemPath {
 public:
  FilesystemPath() = default;
  explicit FilesystemPath(std::string_view path) : path_(path) {}
  FilesystemPath(std::string_view path_up_to, std::string_view filename);
  FilesystemPath(std::string_view path_up_to, const struct dirent &dirent);

  FilesystemPath(FilesystemPath &&) = default;
  FilesystemPath(const FilesystemPath &) = default;
  FilesystemPath &operator=(const FilesystemPath &) = default;

  const std::string &path() const { return path_; }

  // Operating system functions might need a nul-terminated string.
  const char *c_str() const { return path_.c_str(); }

  // The element after the last slash.
  std::string_view filename() const;

  // Some predicates we use.
  bool can_read() const;
  bool is_directory() const;
  bool is_symlink() const;

  // Ideally, this should be canonicalized paths, but this is good enough.
  bool operator<(const FilesystemPath &other) const {
    return path_ < other.path_;
  }
  bool operator==(const FilesystemPath &other) const {
    return path_ == other.path_;
  }

 private:
  enum class MemoizedResult : char { kUnknown, kNo, kYes };
  std::string path_;

  // memoized start of filename.
  mutable size_t filename_offset_ = std::string::npos;

  // Memoized results are updated in const methods and ok to have them mutable.
  mutable MemoizedResult can_read_ = MemoizedResult::kUnknown;
  mutable MemoizedResult is_dir_ = MemoizedResult::kUnknown;
  mutable MemoizedResult is_symlink_ = MemoizedResult::kUnknown;
};

// Given a shell-globbing pattern, return all the matching files.
std::vector<FilesystemPath> Glob(std::string_view glob_pattern);

// Given a filename, read the content of the file into a string. If there was
// an error, return a nullopt.
std::optional<std::string> ReadFileToString(const FilesystemPath &filename);

// Collect files found recursively (BFS) and return.
// Uses predicate "want_dir_p" to check if directory should be entered, and
// "want_file_p" if file should be included; if so, it is added to "paths".
//
// Result only contains files, never directories.
std::vector<FilesystemPath> CollectFilesRecursive(
  const FilesystemPath &dir,
  const std::function<bool(const FilesystemPath &)> &want_dir_p,
  const std::function<bool(const FilesystemPath &)> &want_file_p);
}  // namespace bant

#endif  // BANT_FILE_UTILS_H
