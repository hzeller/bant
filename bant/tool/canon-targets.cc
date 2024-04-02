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

#include "bant/tool/canon-targets.h"

#include "bant/types-bazel.h"
#include "bant/util/query-utils.h"

namespace bant {
void CreateCanonicalizeEdits(Session &session, const ParsedProject &project,
                             const BazelPattern &pattern,
                             const EditCallback &emit_canon_edit) {
  std::ostream &info_out = session.info();
  Stat &stats = session.GetStatsFor("Canonicalization checked", "dependencies");
  ScopedTimer timer(&stats.duration);

  for (const auto &[_, parsed_package] : project.ParsedFiles()) {
    if (!pattern.Match(parsed_package->package)) {
      continue;
    }
    const BazelPackage &current_package = parsed_package->package;
    query::FindTargets(
      parsed_package->ast, {"cc_library", "cc_binary", "cc_test"},
      [&](const query::Result &target) {
        auto self = BazelTarget::ParseFrom(target.name, current_package);
        if (!self.has_value()) {
          return;
        }
        if (!pattern.Match(*self)) {
          return;
        }
        std::vector<std::string_view> deps;
        query::ExtractStringList(target.deps_list, deps);

        for (std::string_view dep_str : deps) {
          stats.count++;
          auto dep_target = BazelTarget::ParseFrom(dep_str, current_package);
          if (!dep_target.has_value()) {
            parsed_package->source.Loc(info_out, dep_str)
              << " Invalid target name '" << dep_str << "'\n";
            continue;
          }
          if (dep_str != dep_target->ToStringRelativeTo(current_package)) {
            emit_canon_edit(EditRequest::kRename, *self, dep_str,
                            dep_target->ToStringRelativeTo(current_package));
          }
        }
      });
  }
}
}  // namespace bant
