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

#include "bant/explore/aliased-by.h"

#include "bant/explore/project-walker.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/types-bazel.h"
#include "bant/types.h"

namespace bant {
OneToN<BazelTarget, BazelTarget> ExtractAliasedBy(const ParsedProject &p) {
  OneToN<BazelTarget, BazelTarget> aliased_by;
  const ProjectWalker walker(p);
  walker.FindTargets(
    {"alias"}, [&](const BazelPackage &package, const BazelTarget &alias,
                   const query::Result &details) {
      auto actual = BazelTarget::ParseFrom(details.actual, package);
      if (!actual.has_value()) return;
      aliased_by[*actual].push_back(alias);
    });
  return aliased_by;
}
}  // namespace bant
