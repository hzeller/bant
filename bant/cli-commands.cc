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

#include "bant/cli-commands.h"

#include <cstdlib>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "bant/explore/aliased-by.h"
#include "bant/explore/dependency-graph.h"
#include "bant/explore/header-providers.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/elaboration.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/print-visitor.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/types.h"
#include "bant/util/file-utils.h"
#include "bant/util/stat.h"
#include "bant/util/table-printer.h"
#include "bant/workspace.h"

// Tools accessible by these commands
#include "bant/tool/canon-targets.h"
#include "bant/tool/compilation-db.h"
#include "bant/tool/dwyu.h"
#include "bant/tool/edit-callback.h"
#include "bant/tool/workspace.h"

namespace bant {
using ::bant::query::FindTargets;
using ::bant::query::Result;

namespace {
// TODO: make flag ? This is needed for projects that don't use a plain
// WORKSPACE but obfuscate the dependencies by loading a bunch of *.bzl
// files (looking at you, XLS...)
constexpr bool kAugmentWorkspacdFromDirectoryStructure = true;

enum class Command {
  kNone,
  kParse,
  kPrint,  // Like parse, but print; if (!print_ast) narrow with pattern.
  kListPackages,
  kListTargets,
  kListLeafs,
  kListWorkkspace,
  kTargetHdrs,
  kTargetSrcs,
  kTargetData,
  kExpandedLibraryHeaders,
  kAliasedBy,
  kGenruleOutputs,
  kDWYU,
  kCompilationDB,
  kCompileFlags,
  kCanonicalizeDeps,
  kHasDependents,
  kDependsOn,
};

void PrintOneToN(bant::Session &session, const BazelTargetMatcher &pattern,
                 const OneToN<BazelTarget, BazelTarget> &table,
                 const std::string &header1, const std::string &header2) {
  auto printer = TablePrinter::Create(
    session.out(), session.flags().output_format, {header1, header2});
  std::vector<std::string> repeat_print;
  for (const auto &d : table) {
    if (!pattern.Match(d.first)) continue;
    repeat_print.clear();
    for (const BazelTarget &t : d.second) {
      repeat_print.emplace_back(t.ToString());
    }
    printer->AddRowWithRepeatedLastColumn({d.first.ToString()}, repeat_print);
  }
  printer->Finish();
}

static bool NeedsProjectPopulated(Command cmd,
                                  const BazelTargetMatcher &pattern) {
  // No need to even parse the project if we just print the full workspace
  if (cmd == Command::kListWorkkspace && !pattern.HasFilter()) {
    return false;  // NOLINT(readability-simplify-boolean-expr)
  }
  return true;
}

// Not supported commands for debuggingg
// If this is a debug command, run this here.
// Right now, this is just a bare parsing/printing of a file (-F <filename>)
std::optional<CliStatus> RunDebugCommand(Session &session, Command cmd) {
  if (session.flags().direct_filename.empty()) {
    return std::nullopt;  // Currently only -F will trigger debug command.
  }
  if (cmd != Command::kParse && cmd != Command::kPrint) {
    session.error() << "-F <filename> only works for 'parse' or 'print'\n";
    return CliStatus::kExitFailure;
  }

  // Parse the file. Not part of parsed-project, just plain here.
  const FilesystemPath file(session.flags().direct_filename);
  // Ok, parse single file.
  Stat open_and_read_stat;
  std::optional<std::string> content =
    ReadFileToStringUpdateStat(file, open_and_read_stat);
  if (!content.has_value()) {
    session.info() << "Could not read " << file.path() << "\n";
    return CliStatus::kExitFailure;
  }
  ParsedProject project({}, /*verbose=*/true, /*with_builtin_macros=*/true);
  const BazelPackage package("", file.parent_path());
  ParsedBuildFile *parsed = project.AddBuildFileContent(
    session, package, file, *content, open_and_read_stat);
  if (!parsed) {
    return CliStatus::kExitFailure;
  }

  if (session.flags().elaborate) {
    const ElaborationOptions options{.builtin_macro_expansion =
                                       session.flags().builtin_macro_expand};
    Elaborate(session, &project, options, parsed);
  }
  if (cmd == Command::kPrint && parsed->ast) {
    PrintVisitor printer(session.out(), session.flags().do_color);
    printer.WalkNonNull(parsed->ast);
    session.out() << "\n";
  }
  return CliStatus::kExitSuccess;
}

CliStatus RunCommand(Session &session, Command cmd,
                     const BazelPatternBundle &patterns) {
  // -- TODO: a lot of the following functionality including choosing what
  // data is needed needs to move into each command itself.
  // We don't have a 'Command' object yet, so linear here.
  auto workspace_or = bant::LoadWorkspace(session);
  if (!workspace_or.has_value()) {
    session.error()
      << "Didn't find any workspace file. Is this a bazel project root ?\n";
    return CliStatus::kExitFailure;
  }
  if (kAugmentWorkspacdFromDirectoryStructure) {
    BestEffortAugmentFromExternalDir(session, workspace_or.value());
  }
  const bant::BazelWorkspace &workspace = workspace_or.value();

  // Matchall pattern bundle.
  BazelPatternBundle kMatchAllBundle;
  kMatchAllBundle.Finish();

  // Has dependent needs to be able to see all the files to know everything
  // that depends on a specific pattern.
  const BazelPatternBundle &dep_pattern =
    (cmd == Command::kHasDependents) ? kMatchAllBundle : patterns;

  CommandlineFlags flags = session.flags();

  bant::ParsedProject project(workspace, flags.verbose);
  if (NeedsProjectPopulated(cmd, patterns)) {
    Stat &stats = session.GetStatsFor("Initial load from pattern", "packages");
    const ScopedTimer timer(&stats.duration);
    const int packages_added = project.FillFromPattern(session, dep_pattern);
    if (packages_added == 0) {
      session.error() << "Pattern did not match any dir with BUILD file.\n";
    }
    stats.count += packages_added;
  }

  if (flags.recurse_dependency_depth <= 0 &&
      (cmd == Command::kDWYU || cmd == Command::kHasDependents)) {
    constexpr int kReasonableDefaultDependencyDepth = 4;
    flags.recurse_dependency_depth = kReasonableDefaultDependencyDepth;
  }

  // For most operations and least surprises, we want to elaborate.
  // Only for print and parse we give finer control
  if (cmd != Command::kParse && cmd != Command::kPrint) {
    flags.elaborate = true;
    flags.builtin_macro_expand = true;
  }

  if (flags.elaborate) {
    const ElaborationOptions options{.builtin_macro_expansion =
                                       flags.builtin_macro_expand};
    bant::Elaborate(session, &project, options);
  }

  // TODO: move dependency graph creation to interested tools once they are
  // Command-objects.
  bant::DependencyGraph graph;
  switch (cmd) {
  case Command::kDWYU:
  case Command::kParse:
  case Command::kTargetHdrs:
  case Command::kTargetData:
  case Command::kExpandedLibraryHeaders:
  case Command::kTargetSrcs:
  case Command::kGenruleOutputs:
  case Command::kListTargets:
  case Command::kListLeafs:
  case Command::kListPackages:
  case Command::kDependsOn:
  case Command::kHasDependents:
    if (flags.recurse_dependency_depth >= 0) {
      const size_t before_build_files = project.ParsedFiles().size();
      graph = bant::BuildDependencyGraph(
        session, dep_pattern, flags.recurse_dependency_depth, &project);
      const size_t after_build_files = project.ParsedFiles().size();
      if (session.flags().verbose) {
        session.info() << "Dependency graph expanded build file# from initial "
                       << before_build_files << " to " << after_build_files
                       << "; " << graph.depends_on.size() << " targets and "
                       << graph.has_dependents.size()
                       << " that depend on these.\n";
        // Currently, we're not using the graph yet, just use it as a way to
        // populate project.
      }
    }
    break;
  default:;
  }

  // library headers and genrule outputs just match the pattern unless
  // recursive is chosen when we want to print everything the dependency graph
  // gathered.
  const BazelPatternBundle &print_pattern =
    flags.recurse_dependency_depth > 0 ? kMatchAllBundle : patterns;

  // This will be all separate commands in their own class.
  switch (cmd) {
  case Command::kPrint:
  case Command::kParse: {
    // Parsing has already be done by now by building the dependency graph,
    // so it would already have emitted parse errors. Here we only have to
    // decide if we print anything.
    if (flags.print_ast || cmd == Command::kPrint || flags.print_only_errors) {
      const auto [count, total] =
        bant::PrintProject(session, patterns, project);
      if (count == 0) {
        session.info() << "No";
      } else {
        session.info() << count;
      }
      const char *const kind = flags.print_ast ? " toplevel nodes" : " rules";
      session.info() << kind << " matched (from " << total;
      if (!flags.print_ast) {
        session.info() << " toplevel nodes; use -a to not narrow to rules";
      }
      if (!flags.elaborate) {
        session.info() << "; use -e to evaluate first";
      }
      session.info() << ")\n";
    }
    break;
  }
  case Command::kExpandedLibraryHeaders:  //
    bant::PrintProvidedSources(
      session, "header", print_pattern,
      ExtractExpandedHeaderToLibMapping(project, session.info()));
    break;

    // TODO: these target srcs/hdrs/data should include target type.
  case Command::kTargetSrcs:  //
    bant::PrintProvidedSources(
      session, "srcs", print_pattern,
      ExtractComponentToTargetMapping(project, ExtractComponent::kSrcs,
                                      session.flags().only_physical_files,
                                      session.info()));
    break;

  case Command::kTargetHdrs:  //
    bant::PrintProvidedSources(
      session, "hdrs", print_pattern,
      ExtractComponentToTargetMapping(project, ExtractComponent::kHdrs,
                                      session.flags().only_physical_files,
                                      session.info()));
    break;

  case Command::kTargetData:  //
    bant::PrintProvidedSources(
      session, "data", print_pattern,
      ExtractComponentToTargetMapping(project, ExtractComponent::kData,
                                      session.flags().only_physical_files,
                                      session.info()));
    break;

  case Command::kGenruleOutputs:
    bant::PrintProvidedSources(
      session, "generated-file", print_pattern,
      ExtractGeneratedFromGenrule(project, session.info()));
    break;

  case Command::kDWYU:
    if (bant::CreateDependencyEdits(
          session, project, patterns,
          CreateBuildozerDepsEditCallback(session.out())) > 0) {
      return CliStatus::kExitCleanupFindings;
    }
    break;

  case Command::kCanonicalizeDeps:
    if (CreateCanonicalizeEdits(
          session, project, patterns,
          CreateBuildozerDepsEditCallback(session.out())) > 0) {
      return CliStatus::kExitCleanupFindings;
    }
    break;

  case Command::kListPackages: {
    auto printer = TablePrinter::Create(
      session.out(), session.flags().output_format, {"bazel-file", "package"});
    for (const auto &[package, parsed] : project.ParsedFiles()) {
      printer->AddRow({std::string(parsed->name()), package.ToString()});
    }
    printer->Finish();
  } break;

  case Command::kListLeafs:
  case Command::kListTargets: {
    auto printer =
      TablePrinter::Create(session.out(), session.flags().output_format,
                           {"file-location", "rule", "target"});
    for (const auto &[package, parsed] : project.ParsedFiles()) {
      FindTargets(parsed->ast, {}, [&](const Result &target) {
        auto target_name =
          BazelTarget::ParseFrom(absl::StrCat(":", target.name), package);
        if (!target_name.has_value()) {
          return;
        }
        if (!print_pattern.Match(*target_name)) return;
        if (cmd == Command::kListLeafs &&
            graph.has_dependents.contains(*target_name)) {
          return;
        }
        printer->AddRow({project.Loc(target.name),
                         std::string(target.rule),  //
                         target_name->ToString()});
      });
    }
    printer->Finish();
  } break;

  case Command::kListWorkkspace:
    PrintMatchingWorkspaceExternalRepos(session, project, patterns);
    break;

  case Command::kAliasedBy:
    PrintOneToN(session, print_pattern, bant::ExtractAliasedBy(project),  //
                "actual", "aliased-by");
    break;

  case Command::kDependsOn:
    // If explicitly asked recursively, print all that.
    PrintOneToN(session, print_pattern, graph.depends_on,  //
                "library", "depends-on");
    break;

  case Command::kHasDependents:
    // Print exactly what requested, as we implicitly had to recurse through
    // everything, so print_pattern would be too much.
    PrintOneToN(session, patterns, graph.has_dependents,  //
                "library", "has-dependent");
    break;

  case Command::kCompilationDB:
  case Command::kCompileFlags:
    WriteCompilationFlags(session, patterns, &project,
                          cmd == Command::kCompilationDB);

    break;

  case Command::kNone:  // nop (implicitly done by parsing)
    ;
  }
  return CliStatus::kExitSuccess;
}
}  // namespace

CliStatus RunCliCommand(Session &session, std::span<std::string_view> args) {
  // Commands: right now just switch/casing over it in main, but they will
  // become their own classes eventually.
  Command cmd = Command::kNone;
  static const std::map<std::string_view, Command> kCommandNames = {
    {"parse", Command::kParse},
    {"print", Command::kPrint},
    {"list-packages", Command::kListPackages},
    {"list-targets", Command::kListTargets},
    {"list-leafs", Command::kListLeafs},
    {"workspace", Command::kListWorkkspace},
    {"target-hdrs", Command::kTargetHdrs},
    {"target-data", Command::kTargetData},
    {"target-srcs", Command::kTargetSrcs},
    {"lib-headers", Command::kExpandedLibraryHeaders},
    {"aliased-by", Command::kAliasedBy},
    {"depends-on", Command::kDependsOn},
    {"has-dependents", Command::kHasDependents},
    {"genrule-outputs", Command::kGenruleOutputs},
    {"dwyu", Command::kDWYU},
    {"compilation-db", Command::kCompilationDB},
    {"compile-flags", Command::kCompileFlags},
    {"canonicalize", Command::kCanonicalizeDeps},
  };

  if (!args.empty()) {
    const std::string_view cmd_string = args[0];
    auto found = kCommandNames.lower_bound(cmd_string);
    if (found != kCommandNames.end() && found->first.starts_with(cmd_string)) {
      auto next_command = std::next(found);
      if (next_command != kCommandNames.end() &&
          next_command->first.starts_with(cmd_string)) {
        session.error() << "Command '" << cmd_string
                        << "' too short and ambiguous: [" << found->first
                        << ", " << next_command->first << ", ...\n";
        return CliStatus::kExitCommandlineClarification;
      }
      cmd = found->second;
    }
    if (cmd == Command::kNone) {
      session.error() << "Unknown command prefix '" << cmd_string << "'\n";
      return CliStatus::kExitCommandlineClarification;
    }
    args = args.subspan(1);
  }

  if (cmd == Command::kNone) {
    session.error() << "Command expected\n";
    return CliStatus::kExitCommandlineClarification;
  }

  BazelPatternBundle patterns;
  for (const std::string_view arg : args) {
    if (auto p = BazelPattern::ParseFrom(arg); p.has_value()) {
      patterns.AddPattern(p.value());
    } else {
      session.error() << "Invalid bazel pattern " << arg << "\n";
      return CliStatus::kExitFailure;
    }
  }
  patterns.Finish();

  if (auto dbg_result = RunDebugCommand(session, cmd); dbg_result.has_value()) {
    return *dbg_result;
  }

  // Don't look through everything for these.
  if (cmd == Command::kCanonicalizeDeps || cmd == Command::kDWYU ||
      cmd == Command::kPrint) {
    if (!patterns.HasFilter()) {
      session.error() << "Please provide a bazel pattern for this command.\n"
                      << "Examples: //... or //foo/bar:baz\n";
      return CliStatus::kExitFailure;
    }
  }

  return RunCommand(session, cmd, patterns);
}
}  // namespace bant
