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

#ifdef _WIN32
#define POSIX_COMPATIBLE 0
#else
#define POSIX_COMPATIBLE 1
#endif

#if POSIX_COMPATIBLE
#include <dirent.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/match.h"
#include "absl/strings/resize_and_overwrite.h"
#include "absl/synchronization/mutex.h"
#include "bant/util/filesystem-prewarm-cache.h"

// Development flag to report cache misses
static constexpr bool kDebugCacheMisses = false;

namespace bant {

Filesystem &Filesystem::instance() {
  // We don't care about any cleanup, so make it leak intentionally.
  static Filesystem *instance = new Filesystem();
  return *instance;
}

#if POSIX_COMPATIBLE
static DirectoryEntry::Type FileTypeFromDirent(const dirent *entry) {
  switch (entry->d_type) {
  case DT_LNK: return DirectoryEntry::Type::kSymlink;
  case DT_DIR: return DirectoryEntry::Type::kDirectory;
  case DT_REG: return DirectoryEntry::Type::kRegularFile;
  default: return DirectoryEntry::Type::kOther;
  }
}
#else
static DirectoryEntry::Type FileTypeFromStatus(
  std::filesystem::file_status status) {
  using std::filesystem::file_type;
  switch (status.type()) {
  case file_type::symlink: return DirectoryEntry::Type::kSymlink;
  case file_type::directory: return DirectoryEntry::Type::kDirectory;
  case file_type::regular: return DirectoryEntry::Type::kRegularFile;
  default: return DirectoryEntry::Type::kOther;
  }
}
#endif

void Filesystem::ReadDirectory(std::string_view path, CacheEntry &result) {
#if POSIX_COMPATIBLE
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
#else
  // Fallback implementation with std::filesystem, missing inode in the process
  std::error_code ec;
  const std::filesystem::path dir_path(path);
  std::filesystem::directory_iterator dir_iter(
    dir_path, std::filesystem::directory_options::skip_permission_denied, ec);
  if (ec) return;

  for (auto it = std::filesystem::begin(dir_iter);
       it != std::filesystem::end(dir_iter); it.increment(ec)) {
    if (ec) break;
    const auto &entry = *it;
    const std::filesystem::file_status status = entry.symlink_status(ec);

    result.emplace_back(DirectoryEntry{
      .inode = 0,  // no inode known in non-posix systems.
      .type = ec ? DirectoryEntry::Type::kOther : FileTypeFromStatus(status),
      .name = entry.path().filename().string(),
    });
  }
#endif

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

bool Filesystem::ExistsInDir(std::string_view dir, std::string_view filename) {
  if (dir.empty()) dir = ".";
  DirectoryEntry compare_entry;
  compare_entry.name = filename;

  const auto &dir_content = ReadDir(dir);
  return std::binary_search(dir_content.begin(), dir_content.end(),
                            compare_entry);
}

namespace {
struct NameInDir {
  std::string_view dir;
  std::string_view filename;
};
static NameInDir SplitPath(std::string_view path) {
  NameInDir result;
  static const std::string_view kCurrentDir(".");
  const auto last_slash = path.find_last_of('/');
  result.dir = (last_slash == std::string::npos) ? kCurrentDir
                                                 : path.substr(0, last_slash);
  result.filename =
    (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);
  return result;
}
}  // namespace

bool Filesystem::Exists(std::string_view path) {
  auto dirname = SplitPath(path);
  return ExistsInDir(dirname.dir, dirname.filename);
}

std::optional<DirectoryEntry> Filesystem::StatInDir(std::string_view dir,
                                                    std::string_view filename) {
  if (dir.empty()) dir = ".";
  DirectoryEntry compare_entry;
  compare_entry.name = filename;

  const auto &dir_content = ReadDir(dir);
  auto found =
    std::lower_bound(dir_content.begin(), dir_content.end(), compare_entry);
  if (found == dir_content.end() || found->name != filename) {
    return std::nullopt;
  }
  return *found;
}

// Similar to StatInDir(), but with whole path.
std::optional<DirectoryEntry> Filesystem::StatPath(std::string_view path) {
  auto dirname = SplitPath(path);
  return StatInDir(dirname.dir, dirname.filename);
}

const std::optional<std::string> &Filesystem::ReadFileToString(
  std::string_view path) {
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

  const std::string filename_as_string(path);
  std::optional<std::string> result;
  std::error_code ec;
  const uint64_t filesize = std::filesystem::file_size(filename_as_string, ec);
  if (!ec) {
    FILE *const f = fopen(filename_as_string.c_str(), "rb");
    if (f) {
      const absl::Cleanup file_closer = [f]() { fclose(f); };
      bool success = false;
      std::string content;
      auto copy_file_to_buffer = [f, filesize, &success](
                                   char *buf, std::size_t available) {
        size_t bytes_left = filesize;
        while (bytes_left > 0) {
          const size_t r = fread(buf, 1, bytes_left, f);
          if (r == 0) break;
          bytes_left -= r;
          buf += r;
        }
        success = (bytes_left == 0);
        return filesize;
      };
      absl::StringResizeAndOverwrite(content, filesize, copy_file_to_buffer);
      if (success) result = std::move(content);
    }
  }

  const absl::WriterMutexLock l(file_mutex_);
  if (kDebugCacheMisses && was_new) {
    fprintf(stderr, "File Cache miss for '%s'", std::string{cache_key}.c_str());
    if (result.has_value()) {
      fprintf(stderr, " (%d bytes)\n", static_cast<int>(result->size()));
    } else {
      fprintf(stderr, " (NOT FOUND)\n");
    }
  }
  auto inserted = file_cache_.emplace(cache_key, std::move(result));
  return inserted.first->second;
}

}  // namespace bant
