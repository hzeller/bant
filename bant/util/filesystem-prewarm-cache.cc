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

#include "bant/util/filesystem-prewarm-cache.h"

#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>  // for std::hash
#include <ios>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "bant/util/filesystem.h"
#include "bant/util/thread-pool.h"

namespace bant {
namespace {
static constexpr int kPrewarmParallelism = 32;

class FilesystemPrewarmCache {
 public:
  // Singleton access to the one global caching object.
  static FilesystemPrewarmCache &instance() {
    static FilesystemPrewarmCache sInstance;
    return sInstance;
  }

  void InitCacheFile(const std::string &cache_file);

  bool FileAccessed(std::string_view file) { return WritePrefixed('F', file); }
  bool DirAccessed(std::string_view dir) { return WritePrefixed('D', dir); }

 private:
  bool WritePrefixed(char prefix, std::string_view f) {
    if (!writer_) return false;
    const absl::MutexLock l(&write_lock_);
    if (!already_seen_.insert(std::string{f}).second) return false;
    *writer_ << prefix << f << "\n";
    return true;
  }

  absl::Mutex write_lock_;
  std::unique_ptr<std::fstream> writer_;
  absl::flat_hash_set<std::string> already_seen_ ABSL_GUARDED_BY(write_lock_);
  std::unique_ptr<ThreadPool> pool_;
};

void FilesystemPrewarmCache::InitCacheFile(const std::string &cache_file) {
  std::fstream input(cache_file, std::ios::in | std::ios::binary);
  Filesystem &fs = Filesystem::instance();

  if (input.good()) {
    pool_ = std::make_unique<ThreadPool>(kPrewarmParallelism);
    std::string line;
    while (std::getline(input, line)) {
      if (line.length() < 2) continue;
      const char type = line[0];
      if (type == 'F') {
        pool_->ExecAsync([line]() {
          const int discard [[maybe_unused]] = access(line.c_str() + 1, F_OK);
        });
      } else if (type == 'D') {
        pool_->ExecAsync([line, &fs]() { fs.ReadDir(line.c_str() + 1); });
      }
    }

    pool_->WaitEmpty();
  }
  input.close();

  // Best effort: just overwrite if possible. Failure is ok.
  writer_ = std::make_unique<std::fstream>(
    cache_file, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!writer_->good()) writer_.reset(nullptr);
}
}  // namespace

// -- Public interface

void FilesystemPrewarmCacheInit(int argc, char *argv[]) {
  // If the user created a ~/.cache/bant directory, use that.
  const char *homedir = getenv("HOME");
  if (!homedir) return;
  const std::string cache_dir = absl::StrCat(homedir, "/.cache/bant");
  if (!std::filesystem::is_directory(cache_dir)) return;  // no dir, no cache.

  // Make filename unique to match cwd and arguments.
  std::error_code err;
  const std::filesystem::path cwd = std::filesystem::current_path(err);
  uint64_t argument_dependent_hash = std::hash<std::string>()(cwd.string());
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    // With or without the following flags, same access pattern expected; don't
    // inlude them in the cache uniqifier.
    if (arg == "-v" || arg == "-q" || arg == "-vq" || arg == "-qv" ||
        arg == "-k") {
      continue;
    }

    if (arg == "-C" ||  // already reflected in the cwd
        arg == "-o" || arg == "-f") {
      ++i;  // Skip optarg
      continue;
    }

    argument_dependent_hash ^= std::hash<std::string_view>()(arg);
  }

  const std::string cache_file = absl::StrFormat(
    "%s/fs-warm-%08x-%s", cache_dir, argument_dependent_hash & 0xffff'ffff,
    cwd.filename().c_str());
  FilesystemPrewarmCache::instance().InitCacheFile(cache_file);
}

bool FilesystemPrewarmCacheRememberFileWasAccessed(std::string_view file) {
  return FilesystemPrewarmCache::instance().FileAccessed(file);
}

bool FilesystemPrewarmCacheRememberDirWasAccessed(std::string_view dir) {
  return FilesystemPrewarmCache::instance().DirAccessed(dir);
}

}  // namespace bant
