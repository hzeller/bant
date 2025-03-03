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

#include "bant/explore/dependency-graph.h"

#include <initializer_list>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "bant/explore/header-providers.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/elaboration.h"
#include "bant/frontend/parsed-project.h"
#include "bant/output-format.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/types.h"
#include "bant/util/file-utils.h"
#include "bant/util/stat.h"
#include "bant/util/table-printer.h"
#include "bant/workspace.h"

namespace bant {
namespace {

std::optional<FilesystemPath> PathForPackage(Session &session,
                                             const BazelWorkspace &workspace,
                                             const BazelPackage &package) {
  Stat &stat = session.GetStatsFor("  - exist-check", "BUILD files");

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
  const ScopedTimer timer(&stat.duration);
  for (const std::string_view build_file : {"BUILD", "BUILD.bazel"}) {
    FilesystemPath test_path(start_path, build_file);
    ++stat.count;
    if (test_path.can_read()) return test_path;
  }
  return std::nullopt;
}

void FindAndParseMissingPackages(Session &session,
                                 const std::set<BazelPackage> &want,
                                 std::set<BazelPackage> *error_packages,
                                 ParsedProject *project) {
  const BazelWorkspace &workspace = project->workspace();
  for (const BazelPackage &package : want) {
    if (project->FindParsedOrNull(package) != nullptr) {
      continue;  // have it already.
    }
    auto path = PathForPackage(session, workspace, package);
    if (!path.has_value()) {
      error_packages->insert(package);
      continue;
    }
    // Always elaborate new packages that we add as part of dependency graph
    // building, as it might expand more dpendencies.
    // TODO: but do we need expensive glob() enabled ?
    ParsedBuildFile *file = project->AddBuildFile(session, *path, package);
    bant::Elaborate(session, project, file);
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

// Dependencies that can be
//   * simply files that already exist in the source-tree: don't add
//   * files, that are generated from genrules. Append genrule.
//   * Otherwise: it might just be an existing target that we should find.
//     (if "fallback_is_target", otherwise ignore"
static void AppendPossibleFileDependencies(
  List *list, const BazelWorkspace &workspace,
  const BazelPackage &context_package,
  const OneToOne<std::string, std::string> &generated_by_target,
  bool fallback_is_target, std::vector<std::string_view> &append_to) {
  for (const std::string_view path_or_label : query::ExtractStringList(list)) {
    const std::string as_filename =
      context_package.FullyQualifiedFile(workspace, path_or_label);
    if (FilesystemPath(as_filename).can_read()) {
      continue;  // quick check: regulsr file existing in source tree.
    }

    // Alright, let's resolve this as a target, as this is also
    // a way to refer to a file.
    const auto fqt = BazelTarget::ParseFrom(path_or_label, context_package);
    if (!fqt.has_value()) {
      continue;  // If not parseable as target it will also fail downstream.
    }

    const FilesystemPath path_in_src_tree(fqt->package.path, fqt->target_name);
    if (path_in_src_tree.can_read()) {
      continue;  // Extracted from fully qualified name: looks like actual file.
    }

    // Not an existing file. Is it generated somewhere ?
    auto found_genrule = generated_by_target.find(path_in_src_tree.path());
    if (found_genrule != generated_by_target.end()) {
      append_to.push_back(found_genrule->second);
      continue;
    }

    // Not generated. Let's assume this is a bazel label if requested.
    if (fallback_is_target) {
      append_to.push_back(path_or_label);
    }
  }
}

static OneToOne<std::string, std::string> FlattenTargetsToString(
  const ProvidedFromTarget &string_to_target) {
  OneToOne<std::string, std::string> result;
  for (const auto &[name, value] : string_to_target) {
    result.emplace(name, value.ToString());
  }
  return result;
}
}  // namespace

DependencyGraph BuildDependencyGraph(Session &session,
                                     const BazelTargetMatcher &pattern,
                                     int nesting_depth, ParsedProject *project,
                                     const TargetInGraphCallback &walk_cb) {
  // Follow all rules for now.
  const std::initializer_list<std::string_view> kRulesOfInterest = {};

  // lhs: dependencies to resolve; rhDes: an example where that was requested.
  using NeedDependencyWithOneExample = OneToOne<BazelTarget, BazelTarget>;

  std::set<BazelPackage> error_packages;
  NeedDependencyWithOneExample error_target_example;

  NeedDependencyWithOneExample deps_to_resolve_todo;

  Stat &stat = session.GetStatsFor("Dependency follow iterations", "rounds");
  const ScopedTimer timer(&stat.duration);

  // TODO: the genrules should be expanded as we widen to other packages
  // but typically they are in the same package as the targets we request the
  // dependency graph to start - so this typically yields a good enough result.
  const OneToOne<std::string, std::string> generated_by_target =
    FlattenTargetsToString(
      ExtractGeneratedFromGenrule(*project, session.info()));

  // Build the initial set of targets to follow from the pattern.
  const BazelTarget root_request;
  for (const auto &[_, parsed] : project->ParsedFiles()) {
    const BazelPackage &current_package = parsed->package;
    if (!pattern.Match(parsed->package)) continue;
    query::FindTargets(parsed->ast, kRulesOfInterest,  //
                       [&](const query::Result &result) {
                         auto target_or =
                           current_package.QualifiedTarget(result.name);
                         if (!target_or || !pattern.Match(*target_or)) return;
                         deps_to_resolve_todo[*target_or] = root_request;
                       });
  }

  DependencyGraph graph;
  do {
    ++stat.count;

    // Only need to look in a subset of packages requested by our target todo.
    // All these targets boil down to a set of packages that we need
    // to have available in the project (and possibly parse if not yet).
    std::set<BazelPackage> scan_package;
    for (const auto &[target, _] : deps_to_resolve_todo) {
      scan_package.insert(target.package);
    }

    // Make sure that we have parsed all packages we're looking through.
    FindAndParseMissingPackages(session, scan_package, &error_packages,
                                project);

    NeedDependencyWithOneExample next_round_deps_to_resolve_todo;
    for (const BazelPackage &current_package : scan_package) {
      const auto *parsed = project->FindParsedOrNull(current_package);
      if (!parsed) continue;
      query::FindTargets(
        parsed->ast, kRulesOfInterest, [&](const query::Result &result) {
          const auto target_or = current_package.QualifiedTarget(result.name);
          if (!target_or.has_value()) return;
          const bool interested = (deps_to_resolve_todo.erase(*target_or) == 1);
          if (!interested) return;

          if (walk_cb) {
            walk_cb(*target_or, result);
          }

          // Gather up what this target might depend on. The deps=[] are
          // obvious, but there might also be dependencies due to files, data,
          // and tools we use.

          // deps=[]
          auto to_follow = query::ExtractStringList(result.deps_list);

          // Possible file dependencies, maybe provided by genrules.
          for (List *possible_dep : {result.hdrs_list, result.srcs_list}) {
            AppendPossibleFileDependencies(possible_dep, project->workspace(),
                                           current_package, generated_by_target,
                                           /*fallback_is_target=*/false,
                                           to_follow);
          }

          // data=[] and tools=[] dependencies could be both, files or targets.
          for (List *possible_dep : {result.data_list, result.tools_list}) {
            AppendPossibleFileDependencies(possible_dep, project->workspace(),
                                           current_package, generated_by_target,
                                           /*fallback_is_target=*/true,
                                           to_follow);
          }

          if (!result.actual.empty()) {  // Follow aliases
            to_follow.push_back(result.actual);
          }

          std::vector<BazelTarget> &depends_on =
            graph.depends_on.insert({*target_or, {}}).first->second;

          for (const auto dep : to_follow) {
            auto dependency_or = BazelTarget::ParseFrom(dep, current_package);
            if (!dependency_or.has_value()) continue;

            // If this dependency is a target that we have not seen yet or will
            // see in this round, put in the next todo.
            if (!graph.depends_on.contains(*dependency_or) &&
                !deps_to_resolve_todo.contains(*dependency_or)) {
              next_round_deps_to_resolve_todo.emplace(*dependency_or,
                                                      *target_or);
            }

            depends_on.push_back(*dependency_or);
            // ... and the reverse
            graph.has_dependents[*dependency_or].push_back(*target_or);
          }
        });
    }

    // Leftover dependencies that could not be resolved.
    error_target_example.insert(deps_to_resolve_todo.begin(),
                                deps_to_resolve_todo.end());

    deps_to_resolve_todo = next_round_deps_to_resolve_todo;
  } while (!deps_to_resolve_todo.empty() && (nesting_depth-- > 0));

  if (session.flags().verbose) {
    // Currently, we have a lot of targets that we don't deal with yet, such as
    // genrules or protobuffer rules. Goal: should be zero.
    // But for now: hide behind 'verbose' flag, to not be too noisy.
    if (!error_packages.empty()) {
      PrintList(session.info(), "Dependcy graph: Did not find these packages\n",
                error_packages);
    }
    if (!error_target_example.empty()) {
      session.info() << "Dependency graph: Did not find these targets\n";
      auto printer = TablePrinter::Create(session.info(), OutputFormat::kNative,
                                          {"Dependency", "needed-by"});
      // Ascii table does not have header, so add our own here.
      printer->AddRow({"[--- Dependency ---]", "[--- Example Needed By ---]"});
      for (const auto &[dep, example] : error_target_example) {
        printer->AddRow({dep.ToString(), example.ToString()});
      }
      printer->Finish();
      session.info() << "\n";
    }
  }

  return graph;
}

}  // namespace bant
