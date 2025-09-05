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
#include "bant/util/arena.h"

namespace bant {
DirectoryEntry *DirectoryEntry::Alloc(Arena &where, std::string_view name) {
  DirectoryEntry *entry = static_cast<DirectoryEntry *>(
    where.Alloc(sizeof(DirectoryEntry) + name.size() + 1));
  strncpy(entry->name, name.data(), name.size() + 1);
  return entry;
}

Filesystem &Filesystem::instance() {
  // We don't care about any cleanup, so make it intentionally leak.
  static Filesystem *instance = new Filesystem();
  return *instance;
}

static DirectoryEntry::Type FileTypeFromDirent(const dirent *entry) {
  switch (entry->d_type) {
  case DT_LNK: return DirectoryEntry::Type::kSymlink;
  case DT_DIR: return DirectoryEntry::Type::kDirectory;
  default: return DirectoryEntry::Type::kOther;
  }
}

void Filesystem::ReadDirectory(std::string_view path, CacheEntry &result) {
  const std::string dir_as_string(path);
  DIR *const dir = opendir(dir_as_string.c_str());
  if (!dir) return;

  const absl::Cleanup dir_closer = [dir]() { closedir(dir); };
  while (dirent *const entry = readdir(dir)) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    auto *entry_copy = DirectoryEntry::Alloc(result.data, entry->d_name);
    entry_copy->inode = entry->d_ino;
    entry_copy->type = FileTypeFromDirent(entry);
    result.entries.push_back(entry_copy);
  };
}

void Filesystem::EvictCache() {
  const absl::WriterMutexLock l(&mu_);
  cache_.clear();
}

std::vector<const DirectoryEntry *> Filesystem::ReadDirectory(
  std::string_view path) {
  {
    const absl::ReaderMutexLock l(&mu_);
    if (auto found = cache_.find(path); found != cache_.end()) {
      return found->second.entries;
    }
  }

  // Don't hold lock while filling. We have a local arena in each CacheEntry
  // to avoid mutex-locking it but wasting memory blocks.
  // Might be worthwhile re-evaluating.
  CacheEntry result;
  ReadDirectory(path, result);

  const absl::WriterMutexLock l(&mu_);
  auto inserted = cache_.emplace(path, std::move(result));
  return inserted.first->second.entries;
}
}  // namespace bant
