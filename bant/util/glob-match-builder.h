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

#ifndef BANT_GLOB_MATCH_BUILDER_H
#define BANT_GLOB_MATCH_BUILDER_H

#include <functional>
#include <set>
#include <string>
#include <string_view>

namespace bant {
// A builder taking glob-patterns and building predicates used in filsystem
// walking.
class GlobMatchBuilder {
 public:
  void AddIncludePattern(std::string_view pattern);
  void AddExcludePattern(std::string_view pattern);

  // Build and return a predicate checking if a directory should be traversed
  // while building the glob output.
  std::function<bool(std::string_view)> BuildDirectoryMatchPredicate();

  // Build and return predicate to check if a file shall be included in glob.
  std::function<bool(std::string_view)> BuildFileMatchPredicate();

  // The longest common directory prefix of all include patterns.
  std::string CommonDirectoryPrefix() const;

 private:
  std::set<std::string> include_pattern_;
  std::set<std::string> exclude_pattern_;
};
}  // namespace bant

#endif  // BANT_GLOB_MATCH_BUILDER_H
