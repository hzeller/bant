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

#include "bant/tool/workspace.h"

#include <string>
#include <string_view>
#include <vector>

#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/types.h"
#include "bant/util/file-utils.h"
#include "bant/util/table-printer.h"
#include "bant/workspace.h"

// TODO: output that shows project dependencies, possibly as graphviz.
// (though maybe better as separate command)

namespace bant {
static void PrintExternalRepos(
  Session &session,
  const OneToOne<VersionedProject, FilesystemPath> &external_repos) {
  auto printer =
    TablePrinter::Create(session.out(), session.flags().output_format,
                         {"project", "version", "directory"});
  for (const auto &[project, file] : external_repos) {
    printer->AddRow({project.project,
                     project.version.empty() ? "-" : project.version,
                     file.path()});
  }
  printer->Finish();
}

BazelWorkspace CreateFilteredWorkspace(Session &session,
                                       const ParsedProject &project,
                                       const BazelPattern &pattern) {
  // Look through the project and fish out all the unique projects we see.

  const BazelWorkspace &global_workspace = project.workspace();
  BazelWorkspace matching_workspace_subset;
  for (const auto &[_, parsed_package] : project.ParsedFiles()) {
    const BazelPackage &current_package = parsed_package->package;
    if (!pattern.Match(current_package)) {
      continue;
    }
    query::FindTargetsAllowEmptyName(
      parsed_package->ast, {}, [&](const query::Result &details) {
        std::vector<std::string_view> potential_external_refs;
        if (details.rule == "load") {  // load() calls at package level.
          // load() has positional arguments (and no 'name').
          potential_external_refs =
            query::ExtractStringList(details.node->argument());
        } else {
          // Classical cc_library(), cc_binary() etc that has dependencies.
          auto target = current_package.QualifiedTarget(details.name);
          if (!target.has_value() || !pattern.Match(*target)) {
            return;
          }
          potential_external_refs = query::ExtractStringList(details.deps_list);
          // If alias, look at what it points to
          if (!details.actual.empty()) {
            potential_external_refs.push_back(details.actual);
          }
        }

        // Alright, now let's check these if they reference external projects.
        for (const std::string_view ref : potential_external_refs) {
          const auto ref_target = BazelTarget::ParseFrom(ref, current_package);
          if (!ref_target.has_value()) continue;  // could not parse.

          // We're only interested in printing projects other than our own.
          const std::string &project = ref_target->package.project;
          if (project.empty() || project == current_package.project) continue;

          // If available in global workspace, transfer to our filtered subset.
          const auto found = global_workspace.FindEntryByProject(project);
          if (found == global_workspace.project_location.end()) continue;

          // TODO: maybe actually report where this was ? We have all the info.
          matching_workspace_subset.project_location.insert(*found);
        }
      });
  }
  return matching_workspace_subset;
}

void PrintMatchingWorkspaceExternalRepos(Session &session,
                                         const ParsedProject &project,
                                         const BazelPattern &pattern) {
  const BazelWorkspace &to_print =
    pattern.is_matchall() ? project.workspace()
                          : CreateFilteredWorkspace(session, project, pattern);

  PrintExternalRepos(session, to_print.project_location);
}
}  // namespace bant
