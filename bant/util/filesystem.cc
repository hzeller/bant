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

#include <dirent.h>

#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/synchronization/mutex.h"

namespace bant {
Filesystem &Filesystem::instance() {
  // We don't care about any cleanup, so make it intentionally leak.
  static Filesystem *instance = new Filesystem();
  return *instance;
}

static void ReadDirectoryIntoVector(std::string_view path,
                                    std::vector<DirectoryEntry> &result) {
  const std::string dir_as_string(path);
  DIR *const dir = opendir(dir_as_string.c_str());
  if (!dir) return;

  const absl::Cleanup dir_closer = [dir]() { closedir(dir); };
  while (dirent *const entry = readdir(dir)) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    DirectoryEntry::Type entry_type;
    switch (entry->d_type) {
    case DT_LNK: entry_type = DirectoryEntry::Type::kSymlink; break;
    case DT_DIR: entry_type = DirectoryEntry::Type::kDirectory; break;
    default: entry_type = DirectoryEntry::Type::kOther; break;
    };

    // TODO: copying it over to a new data structure adds overhad that
    // actually makes this slighly slower on a fast filesystem, even
    // if it can cache something. Consider arena and flexible array member.
    result.emplace_back(DirectoryEntry{
      .inode = entry->d_ino,
      .type = entry_type,
      .name = entry->d_name,
    });
  };
}

void Filesystem::EvictCache() {
  const absl::WriterMutexLock l(&mu_);
  cache_.clear();
}

const std::vector<DirectoryEntry> &Filesystem::ReadDirectory(
  std::string_view path) {
  {
    const absl::ReaderMutexLock l(&mu_);
    if (auto found = cache_.find(path); found != cache_.end()) {
      return found->second;
    }
  }

  // Don't hold lock while filling.
  std::vector<DirectoryEntry> result;
  ReadDirectoryIntoVector(path, result);

  const absl::WriterMutexLock l(&mu_);
  auto inserted = cache_.emplace(path, std::move(result));
  return inserted.first->second;
}
}  // namespace bant
