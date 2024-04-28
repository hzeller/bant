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

#include "bant/explore/dependency-graph.h"

#include <initializer_list>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/util/file-utils.h"
#include "bant/util/stat.h"
#include "bant/workspace.h"

namespace bant {
namespace {

std::optional<FilesystemPath> PathForPackage(const BazelWorkspace &workspace,
                                             const BazelPackage &package) {
  std::string start_path;
  if (!package.project.empty()) {
    auto project_path_or = workspace.FindPathByProject(package.project);
    if (!project_path_or.has_value()) {
      // The following message would be too noisy right now as we attempt to
      // read more dependencies than we need.
      // info_out << "Can't find referenced " << package.project << "\n";
      return std::nullopt;
    }
    start_path = project_path_or.value().path();
  }

  if (!package.path.empty()) {
    if (!start_path.empty()) start_path.append("/");
    start_path.append(package.path);
  }
  for (const std::string_view build_file : {"BUILD", "BUILD.bazel"}) {
    FilesystemPath test_path(start_path, build_file);
    if (test_path.can_read()) return test_path;
  }
  return std::nullopt;
}

void FindAndParseMissingPackages(Session &session,
                                 const std::set<BazelPackage> &want,
                                 const BazelWorkspace &workspace,
                                 std::vector<BazelPackage> *error_packages,
                                 ParsedProject *project) {
  for (const BazelPackage &package : want) {
    if (project->FindParsedOrNull(package) != nullptr) {
      continue;  // have it already.
    }
    auto path = PathForPackage(workspace, package);
    if (!path.has_value()) {
      error_packages->push_back(package);
      continue;
    }
    project->AddBuildFile(session, *path, package);
  }
}

template <typename Container>
void PrintList(std::ostream &out, const char *msg, const Container &c) {
  out << msg;
  for (const auto &element : c) {
    out << "\t" << element << "\n";
  }
  out << "\n";
}

}  // namespace

DependencyGraph BuildDependencyGraph(Session &session,
                                     const BazelWorkspace &workspace,
                                     const BazelPattern &pattern,
                                     ParsedProject *project) {
  // TODO: there will be some implicit dependencies: when using files, they
  // might not come from deps we mention, but are provided by genrules.

  // Follow all rules for now.
  const std::initializer_list<std::string_view> kRulesOfInterest = {};

  std::vector<BazelPackage> error_packages;
  std::vector<BazelTarget> error_targets;

  std::set<BazelTarget> deps_to_resolve_todo;

  Stat &stat = session.GetStatsFor("Dependency follow iterations", "rounds");
  const ScopedTimer timer(&stat.duration);

  // Build the initial set of targets to follow from the pattern.
  for (const auto &[_, parsed] : project->ParsedFiles()) {
    const BazelPackage &current_package = parsed->package;
    if (!pattern.Match(parsed->package)) continue;
    query::FindTargets(parsed->ast, kRulesOfInterest,  //
                       [&](const query::Result &result) {
                         auto target_or =
                           BazelTarget::ParseFrom(result.name, current_package);
                         if (!target_or || !pattern.Match(*target_or)) return;
                         deps_to_resolve_todo.insert(*target_or);
                       });
  }

  DependencyGraph graph;
  do {
    ++stat.count;

    // Only need to look in a subset of packages requested by our target todo.
    // All these targets boil down to a set of packages that we need
    // to have available in the project (and possibly parse if not yet).
    std::set<BazelPackage> scan_package;
    for (const BazelTarget &t : deps_to_resolve_todo) {
      scan_package.insert(t.package);
    }

    // Make sure that we have parsed all packages we're looking through.
    FindAndParseMissingPackages(session, scan_package, workspace,
                                &error_packages, project);

    std::set<BazelTarget> next_round_deps_to_resolve_todo;
    for (const BazelPackage &current_package : scan_package) {
      const auto *parsed = project->FindParsedOrNull(current_package);
      if (!parsed) continue;
      query::FindTargets(
        parsed->ast, kRulesOfInterest, [&](const query::Result &result) {
          auto target_or = BazelTarget::ParseFrom(result.name, current_package);
          if (!target_or.has_value()) return;
          const bool interested = (deps_to_resolve_todo.erase(*target_or) == 1);
          if (!interested) return;

          // The list to insert all the dependencies our current target has.
          std::vector<BazelTarget> &depends_on =
            graph.depends_on.insert({*target_or, {}}).first->second;
          for (const auto dep : query::ExtractStringList(result.deps_list)) {
            auto dependency_or = BazelTarget::ParseFrom(dep, current_package);
            if (!dependency_or.has_value()) continue;

            // If this dependency is a target that we have not seen yet or will
            // see in this round, put in the next todo.
            if (!graph.depends_on.contains(*dependency_or) &&
                !deps_to_resolve_todo.contains(*dependency_or)) {
              next_round_deps_to_resolve_todo.insert(*dependency_or);
            }

            depends_on.push_back(*dependency_or);
            // ... and the reverse
            graph.has_dependents[*dependency_or].push_back(*target_or);
          }
        });
    }

    // Leftover dependencies that could not be resolved.
    error_targets.insert(error_targets.end(), deps_to_resolve_todo.begin(),
                         deps_to_resolve_todo.end());

    deps_to_resolve_todo = next_round_deps_to_resolve_todo;
  } while (!deps_to_resolve_todo.empty());

  if (!error_packages.empty()) {
    PrintList(session.info(), "Trouble finding packages\n", error_packages);
  }

  if (session.verbose() && !error_targets.empty()) {
    // Currently, we have a lot of targets that we don't deal with yet, such as
    // genrules or protobuffer rules. Goal: should be zero.
    // But for now: hide behind 'verbose' flag, to not be too noisy.
    PrintList(session.info(), "Could not find these Targets\n", error_targets);
  }

  return graph;
}

}  // namespace bant
