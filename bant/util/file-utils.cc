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
#include <sys/stat.h>
#include <sys/types.h>

#include <cstddef>
#include <cstring>
#include <deque>
#include <functional>
#include <optional>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"

namespace bant {
std::string_view FilesystemPath::filename() const {
  std::string_view full_path(path_);
  auto last_slash = full_path.find_last_of('/');
  if (last_slash != std::string::npos) {
    return full_path.substr(last_slash + 1);
  }
  return full_path;
}

bool FilesystemPath::is_directory() const {
  struct stat s;
  if (stat(c_str(), &s) != 0) return false;  // ¯\_(ツ)_/¯
  return S_ISDIR(s.st_mode);
}

bool FilesystemPath::is_symlink() const {
  struct stat s;
  if (lstat(c_str(), &s) != 0) return false;  // ¯\_(ツ)_/¯
  return S_ISLNK(s.st_mode);
}

std::optional<std::string> ReadFileToString(const FilesystemPath &filename) {
  int fd = open(filename.c_str(), O_RDONLY);
  if (fd < 0) return std::nullopt;
  std::string content;
  if (struct stat s; fstat(fd, &s) == 0) {
    content.resize(s.st_size);
  } else {
    return std::nullopt;
  }
  char *buf = const_cast<char *>(content.data());  // sneaky :)
  size_t bytes_left = content.size();
  while (bytes_left) {
    ssize_t r = read(fd, buf, bytes_left);
    if (r <= 0) break;
    bytes_left -= r;
    buf += r;
  }
  close(fd);
  if (bytes_left) return std::nullopt;
  return content;
}

// TODO: This was previously implemented recursively using
// std::filesystem::directory_iterator() which was noticeably slower.
//
// Compared to the std::filesystem, it is probaly also slightly less portable,
// but for my personal tools, I'll only ever run it on some Unix anyway.
size_t CollectFilesRecursive(
  const FilesystemPath &dir, std::vector<FilesystemPath> &paths,
  const std::function<bool(const FilesystemPath &)> &want_dir_p,
  const std::function<bool(const FilesystemPath &)> &want_file_p) {
  absl::flat_hash_set<ino_t> seen_inode;  // make sure we don't run in circles.
  size_t count = 0;
  std::error_code err;
  std::deque<std::string> directory_worklist;
  directory_worklist.emplace_back(dir.path());
  while (!directory_worklist.empty()) {
    const std::string current_dir = directory_worklist.front();
    directory_worklist.pop_front();

    DIR *dir = opendir(current_dir.c_str());
    if (!dir) continue;

    while (dirent *entry = readdir(dir)) {
      if (!seen_inode.insert(entry->d_ino).second) {
        continue;  // Avoid getting caught in the symbolic-link loop.
      }
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      ++count;
      FilesystemPath file_or_dir(absl::StrCat(current_dir, "/", entry->d_name));
      if (entry->d_type == DT_DIR ||  // Already know is directory. fast-track.
          ((entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) &&
           file_or_dir.is_directory())) {
        if (want_dir_p(file_or_dir)) {
          directory_worklist.emplace_back(file_or_dir.path());
        }
      } else if (want_file_p(file_or_dir)) {
        paths.emplace_back(std::move(file_or_dir));
      }
    }
    closedir(dir);
  }
  return count;
}
}  // namespace bant
