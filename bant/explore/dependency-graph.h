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

#ifndef BANT_UTIL_RESOLVE_PACKAGES_
#define BANT_UTIL_RESOLVE_PACKAGES_

#include <functional>

#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/types.h"

namespace bant {
struct DependencyGraph {
  OneToN<BazelTarget, BazelTarget> depends_on;
  OneToN<BazelTarget, BazelTarget> has_dependents;
};

// Build Dependency graph for all targets matching "pattern".
// Will follow up to "nesting_depth" recursion levels deep.
// A nesting_depty of <= 0 will not follow any targets and only include
// targets matched by pattern. With 1, all the dependencies of the 0-level
// are followed etc..
// Might update "project" with new files that had to be parsed.
//
// If TargetIngraphcallback is non-null, will inform the caller about which
// targets were walked, including details.
using TargetInGraphCallback =
  std::function<void(const BazelTarget &target, const query::Result &details)>;
DependencyGraph BuildDependencyGraph(Session &session,
                                     const BazelTargetMatcher &pattern,
                                     int nesting_depth, ParsedProject *project,
                                     const TargetInGraphCallback &cb = nullptr);
}  // namespace bant

#endif  // BANT_UTIL_RESOLVE_PACKAGES_
