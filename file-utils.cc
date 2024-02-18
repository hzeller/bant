// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include <dirent.h>
#include <sys/types.h>

#include <cstddef>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/container/flat_hash_set.h"

namespace fs = std::filesystem;

namespace bant {
std::optional<std::string> ReadFileToString(const fs::path &filename) {
  std::ifstream is(filename, std::ifstream::binary);
  if (!is.good()) return std::nullopt;
  std::string result;
  char buffer[4096];
  for (;;) {
    is.read(buffer, sizeof(buffer));
    result.append(buffer, is.gcount());
    if (!is.good()) break;
  }
  return result;
}

// TODO: This is a replacement of the former recursive
// std::filesystem::directory_iterator()-based implementation and noticably
// faster.
// However, we still map every file back to a fs::path, so maybe there is
// more to be gained.
// Compared to the std::filesystem, it is probaly also slightly less portable,
// but for my personal tools, I'll only ever run it on Linux.
size_t CollectFilesRecursive(
  const fs::path &dir, std::vector<fs::path> &paths,
  const std::function<bool(const std::filesystem::path &)> &want_dir_p,
  const std::function<bool(const std::filesystem::path &)> &want_file_p) {
  absl::flat_hash_set<ino_t> seen_inode;  // make sure we don't run in circles.
  size_t count = 0;
  std::error_code err;
  std::deque<std::string> directory_worklist;
  directory_worklist.push_back(dir.string());
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
      std::string full_path = absl::StrCat(current_dir, "/", entry->d_name);
      fs::path file_or_dir(full_path);
      if (entry->d_type == DT_DIR ||  // Already know is directory. fast-track.
          ((entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) &&
           fs::is_directory(file_or_dir, err))) {
        if (want_dir_p(file_or_dir)) {
          directory_worklist.emplace_back(full_path);
        }
      } else if (want_file_p(file_or_dir)) {
        paths.emplace_back(file_or_dir);
      }
    }
    closedir(dir);
  }
  return count;
}
}  // namespace bant
