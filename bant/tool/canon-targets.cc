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

#include "bant/tool/canon-targets.h"

#include <cstdlib>
#include <ostream>
#include <string_view>

#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/tool/edit-callback.h"
#include "bant/types-bazel.h"
#include "bant/util/stat.h"

namespace bant {
size_t CreateCanonicalizeEdits(Session &session, const ParsedProject &project,
                               const BazelTargetMatcher &pattern,
                               const EditCallback &emit_canon_edit) {
  size_t edit_counts = 0;
  std::ostream &info_out = session.info();
  Stat &stats = session.GetStatsFor("Canonicalization checked", "dependencies");
  const ScopedTimer timer(&stats.duration);

  for (const auto &[_, parsed_package] : project.ParsedFiles()) {
    if (!pattern.Match(parsed_package->package)) {
      continue;
    }
    const BazelPackage &current_package = parsed_package->package;
    query::FindTargets(
      parsed_package->ast, {}, [&](const query::Result &target) {
        auto self = current_package.QualifiedTarget(target.name);
        if (!self.has_value()) {
          return;
        }
        if (!pattern.Match(*self)) {
          return;
        }

        const auto deps = query::ExtractStringList(target.deps_list);
        for (const std::string_view dep_str : deps) {
          stats.count++;
          auto dep_target = BazelTarget::ParseFrom(dep_str, current_package);
          if (!dep_target.has_value()) {
            project.Loc(info_out, dep_str)
              << " Invalid target name '" << dep_str << "'\n";
            continue;
          }
          if (dep_str != dep_target->ToStringRelativeTo(current_package)) {
            ++edit_counts;
            emit_canon_edit(EditRequest::kRename, *self, dep_str,
                            dep_target->ToStringRelativeTo(current_package));
          }
        }
      });
  }
  return edit_counts;
}
}  // namespace bant
