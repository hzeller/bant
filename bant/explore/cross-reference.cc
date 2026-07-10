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
  for (const auto &[_, parsed_package] : project.ParsedFiles()) {
    const BazelPackage &current_package = parsed_package->package;
    query::FindTargets(
      parsed_package->ast, {}, [&](const query::Result &target) {
        if (target.name.empty()) return;
        auto self = current_package.QualifiedTarget(target.name);
        if (!self.has_value()) return;
        result.emplace(*self, project.GetLocation(target.name));
      });
  }
  return result;
}

std::unique_ptr<CrossReferenceMap> BuildCrossReferences(
  const ParsedProject &project) {
  auto result = std::make_unique<CrossReferenceMap>();
  const TargetToLocation targetLocation = ExtractTargetToLocation(project);
  Filesystem &fs = Filesystem::instance();
  for (const auto &[_, parsed_package] : project.ParsedFiles()) {
    const BazelPackage &current_package = parsed_package->package;
    query::FindTargets(
      parsed_package->ast, {}, [&](const query::Result &details) {
        // If it has a name, just point to that location.
        if (!details.name.empty()) {
          result->Insert(details.name, project.GetLocation(details.name));
        }

        auto srcs_list =
          query::ExtractStringList({details.srcs_list, details.hdrs_list});
        for (std::string_view src : srcs_list) {
          auto fqn =
            current_package.FullyQualifiedFile(project.workspace(), src);
          if (fs.Exists(fqn)) {
            // Actual file that is existing ? Then link to that.
            result->Insert(src, fqn);
            continue;
          }
          // Ok, not a file, maybe some sort of target, e.g. genrule ref ?
          auto qualified = current_package.QualifiedTarget(src);
          if (qualified.has_value()) {
            if (auto found = targetLocation.find(*qualified);
                found != targetLocation.end()) {
              result->Insert(src, found->second);
            }
          }
        }

        // Things that can point to targets. There, we want to link to the
        // place where these files are defined.
        auto target_refs = query::ExtractStringList(
          {details.deps_list, details.impl_deps_list, details.visibility});
        for (std::string_view target : target_refs) {
          auto qualified = current_package.QualifiedTarget(target);
          if (!qualified.has_value()) continue;
          if (auto found = targetLocation.find(*qualified);
              found != targetLocation.end()) {
            result->Insert(target, found->second);
          }
        }
      });
  }

  return result;
}
}  // namespace bant
