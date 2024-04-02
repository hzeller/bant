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

#include "bant/util/dependency-graph.h"

#include <initializer_list>
#include <optional>
#include <set>

#include "absl/strings/str_cat.h"
#include "bant/frontend/project-parser.h"
#include "bant/types-bazel.h"
#include "bant/util/file-utils.h"
#include "bant/util/query-utils.h"
#include "bant/workspace.h"

namespace bant {
namespace {

std::optional<FilesystemPath> PathForPackage(const BazelWorkspace &workspace,
                                             const BazelPackage &package,
                                             std::ostream &info_out) {
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
                                 std::set<BazelPackage> *known_packages,
                                 std::vector<BazelPackage> *error_packages,
                                 ParsedProject *project) {
  std::vector<BazelPackage> package_todo;
  std::set_difference(want.begin(), want.end(),  //
                      known_packages->begin(), known_packages->end(),
                      std::back_inserter(package_todo));
  for (const BazelPackage &package : package_todo) {
    auto path = PathForPackage(workspace, package, session.info());
    if (!path.has_value()) {
      error_packages->push_back(package);
      continue;
    }
    project->AddBuildFile(session, *path, package);
    known_packages->insert(package);
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
  const std::initializer_list<std::string_view> kRulesOfInterest = {
    "cc_library", "cc_test", "cc_binary"};

  std::vector<BazelPackage> error_packages;
  std::vector<BazelTarget> error_targets;

  std::set<BazelPackage> known_packages;
  std::set<BazelTarget> target_todo;

  Stat &stat = session.GetStatsFor("Dependency follow iterations", "rounds");
  ScopedTimer timer(&stat.duration);

  // Build the initial set of targets to follow from the pattern.
  for (const auto &[_, parsed] : project->ParsedFiles()) {
    const BazelPackage &current_package = parsed->package;
    known_packages.insert(current_package);
    if (!pattern.Match(parsed->package)) continue;
    query::FindTargets(parsed->ast, kRulesOfInterest,  //
                       [&](const query::Result &result) {
                         auto target_or =
                           BazelTarget::ParseFrom(result.name, current_package);
                         if (!target_or || !pattern.Match(*target_or)) return;
                         target_todo.insert(*target_or);
                       });
  }

  DependencyGraph graph;
  do {
    ++stat.count;
    if (session.verbose()) {
      //std::cerr << "-- target-todo with " << target_todo.size() << " items\n";
    }

    // Only need to look in a subset of packages requested by our target todo
    std::set<BazelPackage> scan_package;
    for (const auto &t : target_todo) scan_package.insert(t.package);

    // Make sure that we have parsed all packages we're looking through.
    FindAndParseMissingPackages(session, scan_package, workspace,
                                &known_packages, &error_packages, project);

    std::set<BazelTarget> next_target_todo;
    // TODO: provide a lookup given a package from project.
    for (const auto &[_, parsed] : project->ParsedFiles()) {
      const BazelPackage &current_package = parsed->package;
      if (!scan_package.contains(current_package)) continue;  // not interested.
      query::FindTargets(
        parsed->ast, kRulesOfInterest, [&](const query::Result &result) {
          auto target_or = BazelTarget::ParseFrom(result.name, current_package);
          if (!target_or.has_value()) return;
          const bool interested = target_todo.erase(*target_or) == 1;
          // std::cerr << (interested ? " * " : "   ") << *target_or << "\n";
          if (!interested) return;
          // The list to insert to.
          std::vector<BazelTarget> &depends_on =
            graph.depends_on.insert({*target_or, {}}).first->second;
          for (auto dep : query::ExtractStringList(result.deps_list)) {
            auto dependency_or = BazelTarget::ParseFrom(dep, current_package);
            if (!dependency_or.has_value()) continue;

            // If this dependency is a target that we have not seen yet or will
            // see in this round, put in the next todo.
            if (!graph.depends_on.contains(*dependency_or) &&
                !target_todo.contains(*dependency_or)) {
              next_target_todo.insert(*dependency_or);
            }

            depends_on.push_back(*dependency_or);
            graph.has_dependents[*dependency_or].push_back(*target_or);
          }
        });
    }

    // Leftover todos are were not found.
    error_targets.insert(error_targets.end(), target_todo.begin(),
                         target_todo.end());

    target_todo = next_target_todo;
  } while (!target_todo.empty());

  if (!error_packages.empty()) {
    PrintList(session.info(), "Trouble finding packages", error_packages);
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
