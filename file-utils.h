// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
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

#ifndef BANT_FILE_UTILS_H
#define BANT_FILE_UTILS_H

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace bant {
// Given a filename, read the content of the file into a string. If there was
// an error, return a nullopt.
std::optional<std::string> ReadFileToString(
  const std::filesystem::path &filename);

// Collect files found recursively and store in "paths".
// Uses predicate "want_dir_p" to check if directory should be entered, and
// "want_file_p" if file should be included; if so, it is added to "paths".
//
// Returns number of files looked at.
size_t CollectFilesRecursive(
  const std::filesystem::path &dir, std::vector<std::filesystem::path> &paths,
  const std::function<bool(const std::filesystem::path &)> &want_dir_p,
  const std::function<bool(const std::filesystem::path &)> &want_file_p);
}  // namespace bant

#endif  // BANT_FILE_UTILS_H
