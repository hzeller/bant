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

#include "bant/explore/cross-reference.h"

#include <memory>
#include <string_view>

#include "bant/explore/project-walker.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/source-locator.h"
#include "bant/types-bazel.h"
#include "bant/types.h"
#include "bant/util/filesystem.h"

namespace bant {
using TargetToLocation = OneToOne<BazelTarget, FileLocation>;
static TargetToLocation ExtractTargetToLocation(const ParsedProject &project) {
  TargetToLocation result;
  const ProjectWalker walker(project);
  walker.FindTargets(
    {}, [&](const BazelPackage &package, const BazelTarget &target,
            const query::Result &query_target) {
      result.emplace(target, project.GetLocation(query_target.name));
    });
  return result;
}

std::unique_ptr<CrossReferenceMap> BuildCrossReferences(
  const ParsedProject &project) {
  auto result = std::make_unique<CrossReferenceMap>();

  // In the DependencyGraph building, we only looked at one recusion level
  // down following deps, but there might be more targets that we have not
  // seen, as we are looking at all values here that might have bazel labels
  // in non standard places. Simple solution is for the user to use
  // --graph-augment=..., but ideally we would load build-files on demand.
  const TargetToLocation targetLocation = ExtractTargetToLocation(project);

  Filesystem &fs = Filesystem::instance();
  const ProjectWalker walker(project);
  walker.FindTargets(
    {}, [&](const BazelPackage &current_package, const BazelTarget &target,
            const query::Result &details) {
      // Point name to location itself.
      result->Insert(details.name, project.GetLocation(details.name));

      // Everything else is looked at if it is a target or file
      std::vector<std::string_view> candiates;
      query::KwMap all_fun_values = query::ExtractKwArgs(details.node);
      for (const auto &[_, rhs] : all_fun_values) {
        if (Scalar *scalar = rhs->CastAsScalar(); scalar) {
          candiates.emplace_back(scalar->AsString());
        } else if (List *list = rhs->CastAsList(); list) {
          query::AppendStringList(list, candiates);
        }
      }

      for (std::string_view maybe_xrefable : candiates) {
        auto as_file = current_package.FullyQualifiedFile(project.workspace(),
                                                          maybe_xrefable);
        if (fs.Exists(as_file)) {
          // Actual file that is existing ? Then link to that.
          result->Insert(maybe_xrefable, as_file);
          continue;
        }

        auto as_target = current_package.QualifiedTarget(maybe_xrefable);
        if (as_target.has_value()) {
          if (auto found = targetLocation.find(*as_target);
              found != targetLocation.end()) {
            result->Insert(maybe_xrefable, found->second);
          }
        }
      }
    });

  return result;
}
}  // namespace bant
