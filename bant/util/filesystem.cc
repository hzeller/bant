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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/match.h"
#include "absl/synchronization/mutex.h"
#include "bant/util/filesystem-prewarm-cache.h"

// Development flag to report cache misses
static constexpr bool kDebugCacheMisses = false;

namespace bant {
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

    result.emplace_back(DirectoryEntry{
      .inode = entry->d_ino,
      .type = FileTypeFromDirent(entry),
      .name = entry->d_name,
    });
  }

  // Keep them sorted, so we generate a reproducible output and we can
  // also find them easily with binary search.
  std::sort(result.begin(), result.end());
}

void Filesystem::EvictCache() {
  const absl::WriterMutexLock l(dir_mutex_);
  dir_cache_.clear();
}

static std::string_view LightlyCanonicalizeAsCacheKey(std::string_view path) {
  while (path.size() > 1 && absl::EndsWith(path, "/")) path.remove_suffix(1);
  if (absl::StartsWith(path, "./")) {
    return path.length() > 2 ? path.substr(2) : path.substr(0, 1);
  }
  return path;
}

void Filesystem::SetAlwaysReportEmptyDirectory(std::string_view path) {
  const std::string_view cache_key = LightlyCanonicalizeAsCacheKey(path);
  const absl::WriterMutexLock l(dir_mutex_);
  dir_cache_[cache_key].clear();
}

const std::vector<DirectoryEntry> &Filesystem::ReadDir(
  std::string_view dirpath) {
  const std::string_view cache_key = LightlyCanonicalizeAsCacheKey(dirpath);

  // Note: will only start writing after the initial pre-warm is finished,
  [[maybe_unused]] const bool was_new =
    FilesystemPrewarmCacheRememberDirWasAccessed(cache_key);

  {
    const absl::ReaderMutexLock l(dir_mutex_);
    if (auto found = dir_cache_.find(cache_key); found != dir_cache_.end()) {
      return found->second;
    }
  }

  // Don't hold lock while populating.
  CacheEntry result;
  ReadDirectory(dirpath, result);

  const absl::WriterMutexLock l(dir_mutex_);
  if (kDebugCacheMisses && was_new) {
    fprintf(stderr, "Dir Cache miss for '%s' (%d entries)\n",
            std::string{cache_key}.c_str(), static_cast<int>(result.size()));
  }
  auto inserted = dir_cache_.emplace(cache_key, std::move(result));
  return inserted.first->second;
}

bool Filesystem::Exists(std::string_view path) {
  static const std::string_view kCurrentDir(".");
  const auto last_slash = path.find_last_of('/');
  const std::string_view dir = (last_slash == std::string::npos)
                                 ? kCurrentDir
                                 : path.substr(0, last_slash);
  const std::string_view filename =
    (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);

  DirectoryEntry compare_entry;
  compare_entry.name = filename;

  const auto &dir_content = ReadDir(dir);
  return std::binary_search(dir_content.begin(), dir_content.end(),
                            compare_entry);
}

std::optional<std::string> Filesystem::ReadFileToString(std::string_view path) {
  const std::string_view cache_key = LightlyCanonicalizeAsCacheKey(path);
  // Note: will only start writing after the initial pre-warm is finished,
  [[maybe_unused]] const bool was_new =
    FilesystemPrewarmCacheRememberFileWasAccessed(cache_key);

  {
    const absl::ReaderMutexLock l(file_mutex_);
    if (auto found = file_cache_.find(cache_key); found != file_cache_.end()) {
      return found->second;
    }
  }

  std::string filename_as_string(path);
  const int fd = open(filename_as_string.c_str(), O_RDONLY);
  if (fd < 0) return std::nullopt;
  const absl::Cleanup fd_closer = [fd]() { close(fd); };
  struct stat st;
  if (fstat(fd, &st) != 0) return std::nullopt;

  const size_t filesize = st.st_size;
  bool success = false;
  std::string content;
  auto copy_file_to_buffer = [fd, filesize, &success](char *buf,
                                                      std::size_t available) {
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
  };
#if __cplusplus >= 202100L  // Implemented in gcc since 202100
  content.resize_and_overwrite(filesize, copy_file_to_buffer);
#else
  content.resize(filesize);
  copy_file_to_buffer(const_cast<char *>(content.data()), filesize);
#endif
  if (!success) return std::nullopt;

  const absl::WriterMutexLock l(file_mutex_);
  if (kDebugCacheMisses && was_new) {
    fprintf(stderr, "File Cache miss for '%s' (%d bytes)\n",
            std::string{cache_key}.c_str(), static_cast<int>(content.size()));
  }
  auto inserted = file_cache_.emplace(cache_key, std::move(content));
  return inserted.first->second;
}

}  // namespace bant
