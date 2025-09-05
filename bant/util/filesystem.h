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

#ifndef BANT_FILESYSTEM_H
#define BANT_FILESYSTEM_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "bant/util/arena.h"

namespace bant {

// Platform-independent `struct dirent`-like struct with only the things
// we're interested in; compactly represented.
struct DirectoryEntry {
  // Allocate in Arena (currently only available way to construct). Allocation
  // size is compact and dependent on name.
  static DirectoryEntry *Alloc(Arena &where, std::string_view name);

  enum class Type : uint8_t {
    kOther,
    kDirectory,
    kSymlink,
  };

  uint64_t inode = 0;
  Type type = Type::kOther;
  char name[];  // Allocated as needed.
};

// Very rudimentary filesystem. Right now only used as intermediary to
// cache readdir() results, but could be a start for a filesystem abstraction
// later (e.g. providing stat() and open file).
class Filesystem {
 public:
  // Currently only one global filesystem instance.
  static Filesystem &instance();

  // Equivalent of opendir()/loop readdir() and return all DirectoryEntries.
  // Might return cached results.
  std::vector<const DirectoryEntry *> ReadDirectory(std::string_view path);

  // Evict cache. Might be needed in unit tests.
  void EvictCache();

 private:
  struct CacheEntry {
    Arena data{1024};
    std::vector<const DirectoryEntry *> entries;
  };

  static void ReadDirectory(std::string_view path, CacheEntry &result);

  absl::Mutex mu_;
  absl::flat_hash_map<std::string, CacheEntry> cache_;
};
}  // namespace bant
#endif  // BANT_FILESYSTEM_H
