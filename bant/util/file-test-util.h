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

#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

#include "bant/util/filesystem.h"
#include "gtest/gtest.h"

namespace bant::test {

// Change to a temporary test directory and
class ChangeToTmpDir {
 public:
  explicit ChangeToTmpDir(std::string_view base)
      : dir_before_(std::filesystem::current_path(error_receiver_)) {
    auto dir = std::filesystem::path(::testing::TempDir()) / base;
    std::filesystem::create_directory(dir, error_receiver_);
    std::filesystem::current_path(dir, error_receiver_);

    // Since we're changing cwd underneath, can't re-use cached results,
    // because that stores relative to cwd.
    Filesystem::instance().EvictCache();
  }

  ~ChangeToTmpDir() {
    std::filesystem::current_path(dir_before_, error_receiver_);
    Filesystem::instance().EvictCache();
  }

  void touch(std::string_view relative_to, std::string_view file) {
    std::filesystem::path path;
    if (!relative_to.empty()) {
      path = std::filesystem::path(relative_to) / file;
    } else {
      path = file;
    }
    if (path.has_parent_path()) {
      std::filesystem::create_directories(path.parent_path(), error_receiver_);
    }
    const std::ofstream touch(path);  // destructor will flush file.
  }

 private:
  std::error_code error_receiver_;
  std::filesystem::path dir_before_;
};
}  // namespace bant::test
