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

#include <dirent.h>
#include <fcntl.h>
#include <glob.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
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

namespace bant {
FilesystemPath::FilesystemPath(std::string_view path_up_to,
                               std::string_view filename) {
  while (path_up_to.ends_with('/')) path_up_to.remove_suffix(1);
  path_.reserve(path_up_to.length() + 1 + filename.length());
  path_.append(path_up_to).append("/").append(filename);
  filename_offset_ = path_up_to.size() + 1;
}

FilesystemPath::FilesystemPath(std::string_view path_up_to,
                               const struct dirent &dirent)
    : FilesystemPath(path_up_to, dirent.d_name) {
  switch (dirent.d_type) {
  case DT_LNK:
    is_symlink_ = MemoizedResult::kYes;
    is_dir_ = MemoizedResult::kUnknown;  // Only known after following link.
    break;
  case DT_DIR:
    is_dir_ = MemoizedResult::kYes;
    is_symlink_ = MemoizedResult::kNo;  // Since we know it is definitely dir
    break;
  default:;
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

// Test if symbolic link points to a directory and return 'true' if it does.
// Update "out_inode" with the inode at the destination.
static bool FollowLinkTestIsDir(const FilesystemPath &path, ino_t *out_inode) {
  struct stat s;
  if (stat(path.c_str(), &s) != 0) return false;
  *out_inode = s.st_ino;
  return S_ISDIR(s.st_mode);
}

std::vector<FilesystemPath> Glob(std::string_view glob_pattern) {
  const std::string pattern(glob_pattern);
  glob_t glob_list;
  if (glob(pattern.c_str(), 0, nullptr, &glob_list) != 0) {
    return {};
  }
  const absl::Cleanup glob_closer = [&glob_list]() { globfree(&glob_list); };

  std::vector<FilesystemPath> result;
  for (char **path = glob_list.gl_pathv; *path; ++path) {
    result.emplace_back(*path);
  }
  return result;
}

std::optional<std::string> ReadFileToString(const FilesystemPath &filename) {
  const int fd = open(filename.c_str(), O_RDONLY);
  if (fd < 0) return std::nullopt;
  const absl::Cleanup fd_closer = [fd]() { close(fd); };
  struct stat st;
  if (fstat(fd, &st) != 0) return std::nullopt;

  const size_t filesize = st.st_size;
  bool success = false;
  std::string content;
  content.resize_and_overwrite(
    filesize, [fd, filesize, &success](char *buf, std::size_t alloced_size) {
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
    });
  if (!success) return std::nullopt;
  return content;
}

// Best effort on filesystems that don't have inodes; they typically emit some
// placeholder value such as 0 or -1.
// In consequence, loop-detection is essentially disabled for these filesystems.
// If this become an issue:
// TODO: in that case, base loop-detection on realpath() (will be slower).
static bool LooksLikeValidInode(ino_t inode) {
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
  const std::function<bool(const FilesystemPath &)> &want_dir_p,
  const std::function<bool(const FilesystemPath &)> &want_file_p) {
  std::vector<FilesystemPath> result_paths;
  absl::flat_hash_set<ino_t> seen_inode;  // make sure we don't run in circles.

  std::deque<std::string> directory_worklist;
  directory_worklist.emplace_back(dir.path());
  while (!directory_worklist.empty()) {
    const std::string current_dir = directory_worklist.front();
    directory_worklist.pop_front();

    DIR *const dir = opendir(current_dir.c_str());
    if (!dir) continue;
    const absl::Cleanup dir_closer = [dir]() { closedir(dir); };

    while (dirent *const entry = readdir(dir)) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      FilesystemPath file_or_dir(current_dir, *entry);
      ino_t inode = entry->d_ino;  // Might need updating below if entry symlink

      // The dirent might already tell us that this is a directory, or, we have
      // to test it ourselves, e.g. if it is a symlink. Minimize stat() calls.
      const bool is_directory =
        (entry->d_type == DT_DIR ||  // Short-path: already known to be a dir
         ((entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) &&
          FollowLinkTestIsDir(file_or_dir, &inode)));

      if (is_directory) {
        if (LooksLikeValidInode(inode) && !seen_inode.insert(inode).second) {
          continue;  // Avoid getting caught in symbolic-link loops.
        }
        if (want_dir_p(file_or_dir)) {
          directory_worklist.emplace_back(file_or_dir.path());
        }
      } else if (want_file_p(file_or_dir)) {
        result_paths.emplace_back(std::move(file_or_dir));
      }
    }
  }
  return result_paths;
}
}  // namespace bant
