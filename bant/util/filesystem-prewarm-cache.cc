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

#include <dirent.h>
#include <unistd.h>

#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace bant {
namespace {
static constexpr int kPrewarmParallelism = 32;

// Simplistic thread-pool.
class ThreadPool {
 public:
  explicit ThreadPool(int count) {
    while (count--) {
      threads_.push_back(new std::thread(&ThreadPool::Runner, this));
    }
  }

  ~ThreadPool() {
    CancelAllWork();
    for (std::thread *t : threads_) {
      t->join();
      delete t;
    }
  }

  void ExecAsync(const std::function<void()> &fun) {
    lock_.lock();
    work_queue_.push_back(fun);
    lock_.unlock();
    cv_.notify_one();
  }

  void CancelAllWork() {
    lock_.lock();
    exiting_ = true;
    lock_.unlock();
    cv_.notify_all();
  }

 private:
  void Runner() {
    for (;;) {
      std::unique_lock<std::mutex> l(lock_);
      cv_.wait(l, [this]() { return !work_queue_.empty() || exiting_; });
      if (exiting_) return;
      auto process_work_item = work_queue_.front();
      work_queue_.pop_front();
      l.unlock();
      process_work_item();
    }
  }

  std::vector<std::thread *> threads_;
  std::mutex lock_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> work_queue_;
  bool exiting_ = false;
};

class FilesystemPrewarmCache {
 public:
  // Singleton access to the one global caching object.
  static FilesystemPrewarmCache &instance() {
    static FilesystemPrewarmCache sInstance;
    return sInstance;
  }

  void InitCacheFile(const std::string &cache_file);

  void FileWasAccessed(std::string_view file) { WritePrefixed('F', file); }
  void DirWasAccessed(std::string_view dir) { WritePrefixed('D', dir); }

 private:
  void WritePrefixed(char prefix, std::string_view f) {
    if (!writer_) return;
    if (!already_seen_.insert(std::string{f}).second) return;
    *writer_ << prefix << f << "\n";
  }

  std::unique_ptr<std::fstream> writer_;
  absl::flat_hash_set<std::string> already_seen_;
  std::unique_ptr<ThreadPool> pool_;
};

void FilesystemPrewarmCache::InitCacheFile(const std::string &cache_file) {
  std::fstream input(cache_file, std::ios::in | std::ios::binary);
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
        pool_->ExecAsync([line]() {
          DIR *const dir = opendir(line.c_str() + 1);
          if (dir) {
            const auto *discard [[maybe_unused]] = readdir(dir);  // force first
            closedir(dir);
          }
        });
      }
    }

    // As last operation, let's shutdown ourselves.
    pool_->ExecAsync([this]() { pool_->CancelAllWork(); });
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
  auto cwd = std::filesystem::current_path(err);
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
    "%s/fs-warm-%08x", cache_dir, argument_dependent_hash & 0xffff'ffff);
  FilesystemPrewarmCache::instance().InitCacheFile(cache_file);
}

void FilesystemPrewarmCacheRememberFileWasAccessed(std::string_view file) {
  FilesystemPrewarmCache::instance().FileWasAccessed(file);
}

void FilesystemPrewarmCacheRememberDirWasAccessed(std::string_view dir) {
  FilesystemPrewarmCache::instance().DirWasAccessed(dir);
}

}  // namespace bant
