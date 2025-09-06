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
#include "bant/util/file-utils.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_set.h"
#include "bant/util/filesystem-prewarm-cache.h"
#include "bant/util/filesystem.h"
#include "bant/util/glob-match-builder.h"
#include "bant/util/stat.h"

namespace bant {
FilesystemPath::FilesystemPath(std::string_view path_up_to,
                               std::string_view filename) {
  while (path_up_to.ends_with('/')) path_up_to.remove_suffix(1);
  while (filename.starts_with('/')) filename.remove_prefix(1);
  path_.reserve(path_up_to.length() + 1 + filename.length());
  if (!path_up_to.empty()) {
    path_.append(path_up_to).append("/").append(filename);
    filename_offset_ = path_up_to.size() + 1;
  } else {
    path_ = filename;
    filename_offset_ = 0;
  }
}

FilesystemPath::FilesystemPath(std::string_view path_up_to,
                               const DirectoryEntry &dirent)
    : FilesystemPath(path_up_to, dirent.name) {
  switch (dirent.type) {
  case DirectoryEntry::Type::kSymlink:
    is_symlink_ = MemoizedResult::kYes;
    is_dir_ = MemoizedResult::kUnknown;  // Only known after following link.
    break;
  case DirectoryEntry::Type::kDirectory:
    is_dir_ = MemoizedResult::kYes;
    is_symlink_ = MemoizedResult::kNo;  // Since we know it is definitely dir
    break;
  default:
    is_dir_ = MemoizedResult::kNo;
    is_symlink_ = MemoizedResult::kNo;
    break;
  }
}

std::string_view FilesystemPath::filename() const {
  if (filename_offset_ == std::string::npos) {
    const std::string_view full_path(path_);
    auto last_slash = full_path.find_last_of('/');
    filename_offset_ = (last_slash != std::string::npos) ? last_slash + 1 : 0;
  }
  const std::string_view filename = path_;
  return filename.substr(filename_offset_);
}

bool FilesystemPath::can_read() const {
  if (can_read_ == MemoizedResult::kUnknown) {
    can_read_ =
      (access(c_str(), R_OK) == 0) ? MemoizedResult::kYes : MemoizedResult::kNo;
  }
  return (can_read_ == MemoizedResult::kYes);
}

bool FilesystemPath::is_directory() const {
  if (is_dir_ == MemoizedResult::kUnknown) {
    struct stat s;
    if (stat(c_str(), &s) != 0) return false;  // ¯\_(ツ)_/¯
    is_dir_ = (S_ISDIR(s.st_mode)) ? MemoizedResult::kYes : MemoizedResult::kNo;
  }
  return (is_dir_ == MemoizedResult::kYes);
}

bool FilesystemPath::is_symlink() const {
  if (is_symlink_ == MemoizedResult::kUnknown) {
    struct stat s;
    if (lstat(c_str(), &s) != 0) return false;  // ¯\_(ツ)_/¯
    is_symlink_ =
      (S_ISLNK(s.st_mode)) ? MemoizedResult::kYes : MemoizedResult::kNo;
  }
  return (is_symlink_ == MemoizedResult::kYes);
}

// Just in a struct, so that FilesystemPath can be 'friends' :)
// The update_known_is_directory() method should not really be visible.
struct InternalDirectoryStat {
  // Test if symbolic link points to a directory and return 'true' if it does.
  // Update "out_inode" with the inode at the destination.
  // Can't call path.is_directory() as we also want to know the inode.
  static bool FollowLinkTestIsDir(FilesystemPath &path, uint64_t *out_inode) {
    struct stat s;
    if (stat(path.c_str(), &s) != 0) return false;
    *out_inode = s.st_ino;
    return path.update_known_is_directory(S_ISDIR(s.st_mode));
  }
};

std::vector<FilesystemPath> Glob(std::string_view glob_pattern) {
  GlobMatchBuilder matcher;
  matcher.AddIncludePattern(glob_pattern);
  auto recurse_matcher = matcher.BuildRecurseDirMatchPredicate();
  auto accept_matcher = matcher.BuildFileMatchPredicate();
  return CollectFilesRecursive(
    FilesystemPath(matcher.CommonDirectoryPrefix()),
    [&](const FilesystemPath &dir) { return recurse_matcher(dir.path()); },
    [&](const FilesystemPath &file) { return accept_matcher(file.path()); });
}

std::optional<std::string> ReadFileToString(const FilesystemPath &filename) {
  const int fd = open(filename.c_str(), O_RDONLY);
  if (fd < 0) return std::nullopt;
  const absl::Cleanup fd_closer = [fd]() { close(fd); };
  struct stat st;
  if (fstat(fd, &st) != 0) return std::nullopt;

  FilesystemPrewarmCacheRememberFileWasAccessed(filename.path());
  const size_t filesize = st.st_size;
  bool success = false;
  std::string content;
  auto copy_file_to_buffer = [fd, filesize, &success](char *buf,
                                                      std::size_t available) {
    // Need to use filesize; alloced_size is >= requested.
    size_t bytes_left = filesize;
    while (bytes_left) {
      const ssize_t r = read(fd, buf, bytes_left);
      if (r <= 0) break;
      bytes_left -= r;
      buf += r;
    }
    success = (bytes_left == 0);
    return filesize;
  };
#if __cplusplus >= 202100L  // Implemented in gcc since 202100
  content.resize_and_overwrite(filesize, copy_file_to_buffer);
#else
  content.resize(filesize);
  copy_file_to_buffer(const_cast<char *>(content.data()), filesize);
#endif
  if (!success) return std::nullopt;
  return content;
}

std::optional<std::string> ReadFileToStringUpdateStat(
  const FilesystemPath &filename, Stat &fread_stat) {
  std::optional<std::string> content;
  const ScopedTimer timer(&fread_stat.duration);
  content = ReadFileToString(filename);
  if (content.has_value()) {
    ++fread_stat.count;
    fread_stat.AddBytesProcessed(content->size());
  }
  return content;
}

// Best effort on filesystems that don't have inodes; they typically emit some
// placeholder value such as 0 or -1.
// In consequence, loop-detection is essentially disabled for these filesystems.
// If this become an issue:
// TODO: in that case, base loop-detection on realpath() (will be slower).
static bool LooksLikeValidInode(uint64_t inode) {
  // inode numbers at the edges available numbers look suspicous...
  return inode != 0 && (inode & 0xffff'ffff) != 0xffff'ffff;
}

// FYI: This was previously implemented recursively using
// std::filesystem::directory_iterator() which was noticeably slower.
//
// Compared to using std::filesystem, this implementation is probably slightly
// less portable, but I run my personal tools on Posix-Systems anyway.
std::vector<FilesystemPath> CollectFilesRecursive(
  const FilesystemPath &dir,
  const std::function<bool(const FilesystemPath &)> &enter_dir_p,
  const std::function<bool(const FilesystemPath &)> &want_file_or_dir_p) {
  std::vector<FilesystemPath> result_paths;
  absl::flat_hash_set<ino_t> seen_inode;  // make sure we don't run in circles.

  Filesystem &fs = Filesystem::instance();

  std::deque<std::string> directory_worklist;
  directory_worklist.emplace_back(dir.path());
  while (!directory_worklist.empty()) {
    const std::string current_dir = directory_worklist.front();
    directory_worklist.pop_front();

    FilesystemPrewarmCacheRememberDirWasAccessed(current_dir);
    for (const DirectoryEntry *entry : fs.ReadDir(current_dir)) {
      FilesystemPath file_or_dir(current_dir, *entry);
      uint64_t inode = entry->inode;  // Might need updating if entry symlink

      // The dirent might already tell us that this is a directory, or, we have
      // to test it ourselves, e.g. if it is a symlink. Minimize stat() calls.
      const bool is_directory =
        (entry->type == DirectoryEntry::Type::kDirectory ||  // already known.
         (entry->type == DirectoryEntry::Type::kSymlink &&
          InternalDirectoryStat::FollowLinkTestIsDir(file_or_dir, &inode)));

      if (is_directory) {
        if (LooksLikeValidInode(inode) && !seen_inode.insert(inode).second) {
          continue;  // Avoid getting caught in symbolic-link loops.
        }
        if (enter_dir_p(file_or_dir)) {
          directory_worklist.emplace_back(file_or_dir.path());
        }
      }

      if (want_file_or_dir_p(file_or_dir)) {
        result_paths.emplace_back(std::move(file_or_dir));
      }
    }
  }
  return result_paths;
}
}  // namespace bant
