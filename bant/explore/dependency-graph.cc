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

#include <functional>
#include <future>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
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
#include "bant/util/filesystem.h"
#include "bant/util/stat.h"
#include "bant/util/table-printer.h"
#include "bant/util/thread-pool.h"
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

// Result of async read file.
struct ReadFileResult {
  explicit ReadFileResult(const BazelPackage *package) : package(package) {}

  ReadFileResult(ReadFileResult &&other) = default;      // only allow move
  ReadFileResult(const ReadFileResult &other) = delete;  // ... not copy

  const BazelPackage *package;
  std::optional<FilesystemPath> path;
  std::optional<std::string> content;
  Stat read_stats;
};

// Opening new files can be slow on network filesystems, so make an effort
// of using threads if requested.
void FindAndParseMissingPackages(ThreadPool *io_thread_pool, Session &session,
                                 const std::set<BazelPackage> &want,
                                 std::set<BazelPackage> *error_packages,
                                 ParsedProject *project) {
  static constexpr ElaborationOptions kAlwaysMaccroExpand{
    .builtin_macro_expansion = true,
  };
  const BazelWorkspace &workspace = project->workspace();

  // Enqueue file reading into thread pool, then collect results in bottom half.
  std::vector<std::future<ReadFileResult>> async_resolved;
  for (const BazelPackage &package : want) {
    if (project->FindParsedOrNull(package) != nullptr) {
      continue;  // have it already.
    }
    // Note: pointer to BazelPackage is stable during the lifetime.
    const std::function<ReadFileResult()> fun = [&]() {
      ReadFileResult res(&package);
      res.path = PathForPackage(workspace, package);
      if (!res.path.has_value()) {
        return res;
      }
      res.content = ReadFileToStringUpdateStat(*res.path, res.read_stats);
      return res;
    };
    async_resolved.emplace_back(io_thread_pool->ExecAsync(fun));
  }

  // Harvest all the results. NB: This is single threaded, so all operations
  // on session, project and arena are safe without mutexes.
  for (auto &processed : async_resolved) {
    ReadFileResult result = processed.get();
    if (!result.path.has_value() || !result.content.has_value()) {
      error_packages->insert(*result.package);
      continue;
    }

    // Always elaborate new packages that we add as part of dependency graph
    // building, as it might expand more dpendencies.
    // TODO: but do we need expensive glob() enabled ?
    ParsedBuildFile *file = project->AddBuildFileContent(
      session, *result.package, *result.path, std::move(*result.content),
      result.read_stats);
    bant::Elaborate(session, project, kAlwaysMaccroExpand, file);
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

using MaybeDependency = std::optional<std::string_view>;
using AsyncDepenedencyResults = std::vector<std::future<MaybeDependency>>;

// Dependencies that can be
//   * Simply files that already exist in the source-tree: Good, nothing to do.
//   * Files, that are generated from genrules. Use genrule as dependency.
//   * Otherwise: it might just be an existing target that we should find.
//     (if "fallback_is_target", otherwise ignore"
// This involves checking existence of files thus might be slow on network
// file systems. Use io thread pool and add futures to "append_to".
// (TODO: number of parameters get out of hand...)
static void AppendPossibleFileDependencies(
  ThreadPool *io_thread_pool, List *list, const BazelWorkspace &workspace,
  const BazelPackage &context_package,
  const OneToOne<std::string, std::string> &generated_by_target,
  bool fallback_is_target, AsyncDepenedencyResults &append_to) {
  for (const std::string_view path_or_label : query::ExtractStringList(list)) {
    const auto is_relevant_dep = [path_or_label, &workspace, &context_package,
                                  &generated_by_target,
                                  fallback_is_target]() -> MaybeDependency {
      Filesystem &fs = Filesystem::instance();
      const std::string as_filename =
        context_package.FullyQualifiedFile(workspace, path_or_label);
      if (fs.Exists(as_filename)) {
        return std::nullopt;  // physical file existing in source tree.
      }

      // Alright, let's resolve this as a target, as this is also
      // a way to refer to a file.
      const auto fqt = BazelTarget::ParseFrom(path_or_label, context_package);
      if (!fqt.has_value()) {
        return std::nullopt;  // does not look like a label.
      }

      const FilesystemPath path_in_src_tree(fqt->package.path,
                                            fqt->target_name);
      if (fs.Exists(path_in_src_tree.path())) {
        return std::nullopt;  // Looks like physical file.
      }

      // Not an existing file. Is it generated somewhere ?
      auto found_genrule = generated_by_target.find(path_in_src_tree.path());
      if (found_genrule != generated_by_target.end()) {
        return found_genrule->second;
      }

      // Not generated. Let's assume this is a bazel label if requested.
      if (fallback_is_target) {
        return path_or_label;
      }
      return std::nullopt;
    };

    append_to.emplace_back(
      io_thread_pool->ExecAsync<MaybeDependency>(is_relevant_dep));
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
  ThreadPool io_thread_pool(session.flags().io_threads);
  // Follow all rules for now.
  const std::initializer_list<std::string_view> kRulesOfInterest = {};

  // lhs: dependencies to resolve; rhDes: an example where that was requested.
  using NeedDependencyWithOneExample = OneToOne<BazelTarget, BazelTarget>;

  std::set<BazelPackage> error_packages;
  NeedDependencyWithOneExample error_target_example;

  NeedDependencyWithOneExample deps_to_resolve_todo;

  Stat &stat = session.GetStatsFor("Dependency follow iterations",
                                   "rounds w/ elaboration");
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
    FindAndParseMissingPackages(&io_thread_pool, session, scan_package,
                                &error_packages, project);

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
          if (!result.actual.empty()) {  // Follow aliases
            to_follow.push_back(result.actual);
          }

          session.GetStatsFor("  - deps[] dependencies follow  ", "labels")
            .count += to_follow.size();

          {
            Stat &all = session.GetStatsFor("  - checked srcs[],hdrs[],data[]",
                                            "elements");
            const ScopedTimer timer(&all.duration);
            AsyncDepenedencyResults async_checked_deps;
            // Possible file dependencies, maybe provided by genrules.
            for (List *possible_dep : {result.hdrs_list, result.srcs_list}) {
              AppendPossibleFileDependencies(
                &io_thread_pool, possible_dep, project->workspace(),
                current_package, generated_by_target,
                /*fallback_is_target=*/false, async_checked_deps);
            }

            // data=[] and tools=[] dependencies could be both, files or
            // targets.
            for (List *possible_dep : {result.data_list, result.tools_list}) {
              AppendPossibleFileDependencies(
                &io_thread_pool, possible_dep, project->workspace(),
                current_package, generated_by_target,
                /*fallback_is_target=*/true, async_checked_deps);
            }

            all.count += async_checked_deps.size();

            // Harvest the result of async determined need to add dependency.
            Stat &file_add =
              session.GetStatsFor("  - from these, deduced & follow", "labels");
            for (auto &async_result : async_checked_deps) {
              std::optional<std::string_view> maybe_add = async_result.get();
              if (maybe_add.has_value()) {
                to_follow.push_back(*maybe_add);
                ++file_add.count;
              }
            }
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
      PrintList(session.info(),
                "Dependency graph: Did not find these packages\n",
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
