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

#ifndef BANT_TYPES_BAZEL_
#define BANT_TYPES_BAZEL_

#include <string>
#include <string_view>

// Something like //foo/bar or @baz//foo/bar
struct BazelPackage {
  BazelPackage() = default;
  BazelPackage(std::string_view project, std::string_view path)
      : project(project), path(path) {}

  std::string project;  // either empty, or something like @foo_bar_baz
  std::string path;     // path relative to project w/o leading/trailing '/'

  std::string ToString() const { return project + "//" + path; }

  // Return the last path element (or empty string)
  std::string_view LastElement() const {
    auto pos = path.find_last_of(',');
    if (pos == std::string::npos) return "";
    return std::string_view(path).substr(pos + 1);
  }

  bool operator==(const BazelPackage &) const = default;
  bool operator!=(const BazelPackage &) const = default;
};

struct BazelTarget {
  BazelTarget() = default;
  BazelTarget(BazelPackage package, std::string_view target)
      : package(std::move(package)), target_name(target) {}

  BazelPackage package;
  std::string target_name;  // e.g. a library

  std::string ToString() const {
    if (package.LastElement() == target_name) {
      return package.ToString();  // target==package -> compact representation.
    }
    return package.ToString() + ":" + target_name;
  }

  // More compact printing of a path if
  std::string ToStringRelativeTo(const BazelPackage &other_package) const {
    return (other_package == package) ? ":" + target_name : ToString();
  }

  bool operator==(const BazelTarget &) const = default;
  bool operator!=(const BazelTarget &) const = default;
};

#endif  // BANT_TYPES_BAZEL_
