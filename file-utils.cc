// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace bant {
std::optional<std::string> ReadFileToString(const fs::path &filename) {
  std::ifstream is(filename, std::ifstream::binary);
  if (!is.good()) return std::nullopt;
  std::string result;
  char buffer[4096];
  for (;;) {
    is.read(buffer, sizeof(buffer));
    result.append(buffer, is.gcount());
    if (!is.good()) break;
  }
  return result;
}

// TODO: the c++ implementation seems to be pretty slow. Maybe just build
// something around readdir()
size_t CollectFilesRecursive(
  const fs::path &dir, std::vector<fs::path> *paths,
  const std::function<bool(const std::filesystem::path &)> &want_dir_p,
  const std::function<bool(const std::filesystem::path &)> &want_file_p) {
  std::error_code err;
  if (!fs::is_directory(dir, err) || err.value() != 0) return 0;
  if (!want_dir_p(dir)) return 0;

  size_t count = 0;
  for (const fs::directory_entry &e : fs::directory_iterator(dir)) {
    ++count;
    if (e.is_directory()) {
      count += CollectFilesRecursive(e.path(), paths, want_dir_p, want_file_p);
    } else if (want_file_p(e.path())) {
      paths->emplace_back(e.path());
    }
  }
  return count;
}
}  // namespace bant
