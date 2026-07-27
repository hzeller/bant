// bant - Bazel Navigation Tool
// Copyright (C) 2026 Henner Zeller <h.zeller@acm.org>
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

#ifndef BANT_PROJECT_WALKER_
#define BANT_PROJECT_WALKER_

#include <functional>
#include <initializer_list>
#include <string_view>

#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/types-bazel.h"

namespace bant {
// Given project, find all the packages and targets that match the particular
// rule. Convenient wrapper around some common walking pattern.
class ProjectWalker {
 public:
  explicit ProjectWalker(const ParsedProject &project) : project_(project) {}

  using Callback =
    std::function<void(const BazelPackage &package, const BazelTarget &target,
                       const query::Result &result)>;
  void FindTargets(std::initializer_list<std::string_view> rules_of_interest,
                   const Callback &callback) const;

  // Like FindTargets, but skips packages and targets that don't match the pattern.
  void FindTargetsWithPattern(
    const BazelTargetMatcher &pattern,
    std::initializer_list<std::string_view> rules_of_interest,
    const Callback &callback) const;

 private:
  const ParsedProject &project_;
};
}  // namespace bant
#endif  // BANT_PROJECT_WALKER_
