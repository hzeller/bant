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

#include <alloca.h>
#include <dirent.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/match.h"
#include "absl/synchronization/mutex.h"
#include "bant/util/arena.h"
#include "bant/util/filesystem-prewarm-cache.h"

// TODO: combine this with filesytem-prewarm-cache; they are currently
// somewhat cyclicly dependent on each other.

namespace bant {
static bool DirEntryLessThan(const DirectoryEntry *a, const DirectoryEntry *b) {
  return strcmp(a->name, b->name) < 0;
}

DirectoryEntry *DirectoryEntry::Alloc(Arena &where, std::string_view name) {
  DirectoryEntry *entry = static_cast<DirectoryEntry *>(
    where.Alloc(sizeof(DirectoryEntry) + name.size() + 1));
  strncpy(entry->name, name.data(), name.size());
  entry->name[name.size()] = '\0';
  return entry;
}

static DirectoryEntry::Type FileTypeFromDirent(const dirent *entry) {
  switch (entry->d_type) {
  case DT_LNK: return DirectoryEntry::Type::kSymlink;
  case DT_DIR: return DirectoryEntry::Type::kDirectory;
  default: return DirectoryEntry::Type::kOther;
  }
}

Filesystem &Filesystem::instance() {
  // We don't care about any cleanup, so make it leak intentionally.
  static Filesystem *instance = new Filesystem();
  return *instance;
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
  }

  // Keep them sorted, so we generate a reproducible output and we can
  // also find them easily with binary search.
  std::sort(result.entries.begin(), result.entries.end(), DirEntryLessThan);
}

void Filesystem::EvictCache() {
  const absl::WriterMutexLock l(&mu_);
  cache_.clear();
}

static std::string_view LightlyCanonicalizeAsCacheKey(std::string_view path) {
  while (absl::EndsWith(path, "/")) path.remove_suffix(1);
  if (absl::StartsWith(path, "./")) {
    return path.length() > 2 ? path.substr(2) : path.substr(0, 1);
  }
  return path;
}

void Filesystem::SetAlwaysReportEmptyDirectory(std::string_view path) {
  const std::string_view cache_key = LightlyCanonicalizeAsCacheKey(path);
  CacheEntry empty;  // NOLINT(misc-const-correctness) clang-tidy, you're drunk
  const absl::WriterMutexLock l(&mu_);
  auto inserted = cache_.emplace(cache_key, std::move(empty));
  inserted.first->second.entries.clear();  // Empty, even if it was there before
}

const std::vector<const DirectoryEntry *> &Filesystem::ReadDir(
  std::string_view dirpath) {
  // Development flag to report cache misses
  static constexpr bool kDebugCacheMisses = false;

  const std::string_view cache_key = LightlyCanonicalizeAsCacheKey(dirpath);

  // Note: will only start writing after the initial pre-warm is finished,
  [[maybe_unused]] const bool was_new =
    FilesystemPrewarmCacheRememberDirWasAccessed(cache_key);

  {
    const absl::ReaderMutexLock l(&mu_);
    if (auto found = cache_.find(cache_key); found != cache_.end()) {
      return found->second.entries;
    }
  }

  // Don't hold lock while populating. We have a local arena in each CacheEntry
  // to avoid mutex-locking it but wasting memory blocks.
  // Might be worthwhile re-evaluating.
  CacheEntry result;
  ReadDirectory(dirpath, result);

  const absl::WriterMutexLock l(&mu_);
  if (kDebugCacheMisses && was_new) {
    fprintf(stderr, "Cache miss for '%s' (%d entries)\n",
            std::string{cache_key}.c_str(), (int)result.entries.size());
  }
  auto inserted = cache_.emplace(cache_key, std::move(result));
  return inserted.first->second.entries;
}

bool Filesystem::Exists(std::string_view path) {
  static const std::string_view kCurrentDir(".");
  const auto last_slash = path.find_last_of('/');
  const std::string_view dir = (last_slash == std::string::npos)
                                 ? kCurrentDir
                                 : path.substr(0, last_slash);
  const std::string_view filename =
    (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);

  DirectoryEntry *compare_entry = static_cast<DirectoryEntry *>(
    alloca(sizeof(DirectoryEntry) + filename.size() + 1));
  strncpy(compare_entry->name, filename.data(), filename.size());
  compare_entry->name[filename.size()] = '\0';

  const auto &dir_content = ReadDir(dir);
  return std::binary_search(dir_content.begin(), dir_content.end(),
                            compare_entry, DirEntryLessThan);
}

}  // namespace bant
