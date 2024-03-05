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

#ifndef BANT_TYPES_BAZEL_
#define BANT_TYPES_BAZEL_

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>

namespace bant {
// Something like //foo/bar or @baz//foo/bar
struct BazelPackage {
  BazelPackage() = default;
  BazelPackage(std::string_view project, std::string_view path,
               std::string_view version = "")
      : project(project), version(version), path(path) {}

  // Parse and create package if possible.
  static std::optional<BazelPackage> ParseFrom(std::string_view str);

  std::string project;  // either empty, or something like @foo_bar_baz
  std::string version;  // if known. Typically in bzlmod environments.
  std::string path;     // path relative to project w/o leading/trailing '/'

  std::string ToString() const;  // @project//package/path

  // Assemble filename relative to the path.
  std::string QualifiedFile(std::string_view relative_file) const;

  // Return the last path element (or empty string)
  std::string_view LastElement() const;

  auto operator<=>(const BazelPackage &o) const {
    // Note: currently explicitly _not_ taking version into account.
    // The way we print names only considers project and path.
    return std::tie(project, path) <=> std::tie(o.project, o.path);
  }
  bool operator<(const BazelPackage &) const = default;
  bool operator==(const BazelPackage &) const = default;
  bool operator!=(const BazelPackage &) const = default;
};

inline std::ostream &operator<<(std::ostream &o, const BazelPackage &p) {
  return o << p.ToString();
}

struct BazelTarget {
  BazelTarget() = default;
  BazelTarget(BazelPackage package, std::string_view target)
      : package(std::move(package)), target_name(target) {}

  // Parse target from string. Both forms //foo/bar:baz and :baz are
  // supported. The latter case is canonicalized by adding the context package.
  static std::optional<BazelTarget> ParseFrom(std::string_view str,
                                              const BazelPackage &context);

  // Simple litmus test checking if this looks like a //-style or :-style
  // target.
  static bool LooksWellformed(std::string_view str);

  BazelPackage package;
  std::string target_name;  // e.g. a library

  std::string ToString() const;

  // More compact printing of a path if we are already in that package.
  std::string ToStringRelativeTo(const BazelPackage &other_package) const;

  auto operator<=>(const BazelTarget &o) const = default;
  bool operator<(const BazelTarget &) const = default;
  bool operator==(const BazelTarget &) const = default;
  bool operator!=(const BazelTarget &) const = default;
};

inline std::ostream &operator<<(std::ostream &o, const BazelTarget &t) {
  return o << t.ToString();
}

}  // namespace bant

#endif  // BANT_TYPES_BAZEL_
