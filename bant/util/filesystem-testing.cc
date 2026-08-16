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

// Parts of the Filesystem API only used for tests, so we only link it then.

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/synchronization/mutex.h"
#include "bant/util/file-utils.h"
#include "bant/util/filesystem.h"

namespace bant {
void Filesystem::EvictCache() {
  {
    const absl::WriterMutexLock l(dir_mutex_);
    dir_cache_.clear();
  }
  {
    const absl::WriterMutexLock l(file_mutex_);
    file_cache_.clear();
  }
}

void Filesystem::InjectTestFileContents(
  const std::vector<std::pair<std::string_view, std::string_view>> &contents) {
  auto insert_to_dir = [&](std::string_view path, std::string_view filename,
                           DirectoryEntry::Type type) {
    if (filename == ".") return;
    absl::MutexLock dlock(dir_mutex_);
    auto dir_list_inserted = dir_cache_.emplace(path, CacheEntry{});
    CacheEntry &entry = dir_list_inserted.first->second;
    if (ExistsInCachedDirListing(entry, filename)) return;
    entry.emplace_back(
      DirectoryEntry{.type = type, .name = std::string(filename)});
    std::sort(entry.begin(), entry.end());
  };

  absl::MutexLock flock(file_mutex_);
  for (const auto &[name, content] : contents) {
    file_cache_.emplace(LightlyCanonicalizeAsCacheKey(name), content);

    // Populate directories all the way down
    FilesystemPath fs_path(name);
    insert_to_dir(fs_path.parent_path(), fs_path.filename(),
                  DirectoryEntry::Type::kRegularFile);

    // All the remaining parts leading up to this are directories
    for (;;) {
      fs_path = FilesystemPath(fs_path.parent_path());
      insert_to_dir(LightlyCanonicalizeAsCacheKey(fs_path.parent_path()),
                    fs_path.filename(), DirectoryEntry::Type::kDirectory);
      if (fs_path.path().find_first_of('/') == std::string::npos) break;
    }
  }
}

}  // namespace bant
