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

// The following is to work around clang-tidy being confused and not
// understanding that unistd.h indeed provides getopt(). So let's include
// unistd.h for correctness, and then soothe clang-tidy with decls.
// TODO: how make it just work with including unistd.h ?
#include <unistd.h>                            // NOLINT
extern "C" {                                   //
extern char *optarg;                           // NOLINT
extern int optind;                             // NOLINT
int getopt(int, char *const *, const char *);  // NOLINT
}

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/strings/str_cat.h"
#include "bant/explore/aliased-by.h"
#include "bant/explore/dependency-graph.h"
#include "bant/explore/header-providers.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/elaboration.h"
#include "bant/frontend/parsed-project.h"
#include "bant/output-format.h"
#include "bant/session.h"
#include "bant/tool/canon-targets.h"
#include "bant/tool/dwyu.h"
#include "bant/tool/edit-callback.h"
#include "bant/types-bazel.h"
#include "bant/types.h"
#include "bant/util/table-printer.h"
#include "bant/workspace.h"

// Generated from at compile time from git tag or MODULE.bazel version
#include "bant/generated-build-version.h"

#define BOLD  "\033[1m"
#define RED   "\033[1;31m"
#define RESET "\033[0m"

static int print_version() {
  fprintf(stderr,
          "bant v%s <http://bant.build/>\n"
          "Copyright (c) 2024 Henner Zeller. "
          "This program is free software; license GPL 2.0.\n",
          BANT_BUILD_VERSION);
  return EXIT_SUCCESS;
}

static int usage(const char *prog, const char *message, int exit_code) {
  print_version();
  fprintf(stderr, "Usage: %s [options] <command> [bazel-target-pattern]\n",
          prog);
  fprintf(stderr, R"(Options
    -C <directory> : Change to this project directory first (default = '.')
    -q             : Quiet: don't print info messages to stderr.
    -o <filename>  : Instead of stdout, emit command primary output to file.
    -f <format>    : Output format, support depends on command. One of
                   : native (default), s-expr, plist, json, csv
                     Unique prefix ok, so -fs , -fp, -fj or -fc is sufficient.
    -r             : Follow dependencies recursively starting from pattern.
                     Without parameter, follows dependencies to the end.
                     An optional parameter allows to limit the nesting depth,
                     e.g. -r2 just follows two levels after the toplevel
                     pattern. -r0 is equivalent to not providing -r.
    -v             : Verbose; print some stats.
    -h             : This help.

Commands (unique prefix sufficient):
    %s== Parsing ==%s
    print          : Print AST matching pattern. -e : only files w/ parse errors
                   : -b : elaBorate
    parse          : Parse all BUILD files from pattern. Follow deps with -r
                     Emit parse errors. Silent otherwise: No news are good news.

    %s== Extract facts ==%s (Use -f to choose output format) ==
    workspace      : Print external projects found in WORKSPACE.
                     → 3 column table: (project, version, path)

    -- Given '-r', the following also follow dependencies recursively --
    list-packages  : List all BUILD files and the package they define
                     → 2 column table: (buildfile, package)
    list-targets   : List BUILD file locations of rules with matching targets
                     → 3 column table: (buildfile:location, ruletype, target)
    aliased-by     : List targets and the various aliases pointing to it.
                     → 2 column table: (actual, alias*)
    depends-on     : List cc library targets and the libraries they depend on
                     → 2 column table: (target, dependency*)
    has-dependent  : List cc library targets and the libraries that depend on it
                     → 2 column table: (target, dependent*)
    lib-headers    : Print headers provided by cc_library()s matching pattern.
                     → 2 column table: (header-filename, cc-library-target)
    genrule-outputs: Print generated files by genrule()s matching pattern.
                     → 2 column table: (filename, genrule-target)

    %s== Tools ==%s
    dwyu           : DWYU: Depend on What You Use (emit buildozer edit script)
    canonicalize   : Emit rename edits to canonicalize targets.
)",
          BOLD, RESET, BOLD, RESET, BOLD, RESET);

  if (message) {
    fprintf(stderr, "\n%s%s%s\n", RED, message, RESET);
  }
  return exit_code;
}

using bant::BazelPattern;
using bant::BazelTarget;
using bant::CreateBuildozerDepsEditCallback;
using bant::CreateCanonicalizeEdits;
using bant::OutputFormat;
using bant::TablePrinter;
using bant::query::FindTargets;
using bant::query::Result;

void PrintOneToN(bant::Session &session, const BazelPattern &pattern,
                 const OneToN<BazelTarget, BazelTarget> &table,
                 const std::string &header1, const std::string &header2) {
  auto printer = TablePrinter::Create(session.out(), session.output_format(),
                                      {header1, header2});
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

// TODO: this is getting big, time to put Commands into their own objects.
int main(int argc, char *argv[]) {
  // non-nullptr streams if chosen by user.
  std::unique_ptr<std::ostream> user_primary_out;
  std::unique_ptr<std::ostream> user_info_out;

  // Default primary and info outputs.
  std::ostream *primary_out = &std::cout;
  std::ostream *info_out = &std::cerr;

  BazelPattern pattern;

  bant::CommandlineFlags flags;

  // TODO: make flag ? This is needed for projects that don't use a plain
  // WORKSPACE but obfuscate the dependencies by loading a bunch of *.bzl
  // files (looking at you, XLS...)
  constexpr bool kAugmentWorkspacdFromDirectoryStructure = true;

  // Commands: right now just switch/casing over it in main, but they will
  // become their own classes eventually.
  enum class Command {
    kNone,
    kParse,
    kPrint,  // Like parse, but we narrow with pattern
    kListPackages,
    kListTargets,
    kListWorkkspace,
    kLibraryHeaders,
    kAliasedBy,
    kGenruleOutputs,
    kDWYU,
    kCanonicalizeDeps,
    kHasDependents,
    kDependsOn,
  } cmd = Command::kNone;
  static const std::map<std::string_view, Command> kCommandNames = {
    {"parse", Command::kParse},
    {"print", Command::kPrint},
    {"list-packages", Command::kListPackages},
    {"list-targets", Command::kListTargets},
    {"workspace", Command::kListWorkkspace},
    {"lib-headers", Command::kLibraryHeaders},
    {"aliased-by", Command::kAliasedBy},
    {"depends-on", Command::kDependsOn},
    {"has-dependents", Command::kHasDependents},
    {"genrule-outputs", Command::kGenruleOutputs},
    {"dwyu", Command::kDWYU},
    {"canonicalize", Command::kCanonicalizeDeps},
  };
  static const std::map<std::string_view, OutputFormat> kFormatOutNames = {
    {"native", OutputFormat::kNative}, {"s-expr", OutputFormat::kSExpr},
    {"plist", OutputFormat::kPList},   {"csv", OutputFormat::kCSV},
    {"json", OutputFormat::kJSON},     {"graphviz", OutputFormat::kGraphviz},
  };
  int opt;
  while ((opt = getopt(argc, argv, "C:qo:vhpecbf:r::V")) != -1) {
    switch (opt) {
    case 'C': {
      std::error_code err;
      std::filesystem::current_path(optarg, err);
      if (err) {
        std::cerr << "Can't change into directory " << optarg << "\n";
        return 1;
      }
      break;
    }

    case 'q':  //
      user_info_out.reset(new std::ostream(nullptr));
      info_out = user_info_out.get();
      break;

    case 'o':  //
      if (std::string_view(optarg) == "-") {
        primary_out = &std::cout;
        break;
      }
      user_primary_out.reset(new std::fstream(
        optarg, std::ios::out | std::ios::binary | std::ios::trunc));
      if (!user_primary_out->good()) {
        std::cerr << "Could not open '" << optarg << "'\n";
        return 1;
      }
      primary_out = user_primary_out.get();
      break;

    case 'r':
      flags.recurse_dependency_depth = optarg  //
                                         ? atoi(optarg)
                                         : std::numeric_limits<int>::max();
      break;

      // "print" options
    case 'p': flags.print_ast = true; break;
    case 'e': flags.print_only_errors = true; break;
    case 'b': flags.elaborate = true; break;  // TODO: we need long options.
    case 'f': {
      auto found = kFormatOutNames.lower_bound(optarg);
      if (found == kFormatOutNames.end() || !found->first.starts_with(optarg)) {
        return usage(argv[0], "invalid -f format", EXIT_FAILURE);
      }
      flags.output_format = found->second;
    } break;
    case 'v': flags.verbose = true; break;
    case 'V': return print_version();
    default: return usage(argv[0], nullptr, EXIT_SUCCESS);
    }
  }

  if (optind < argc) {
    const std::string_view cmd_string = argv[optind];
    auto found = kCommandNames.lower_bound(cmd_string);
    if (found != kCommandNames.end() && found->first.starts_with(cmd_string)) {
      auto next_command = std::next(found);
      if (next_command != kCommandNames.end() &&
          next_command->first.starts_with(cmd_string)) {
        std::stringstream sout;
        sout << "Command '" << cmd_string << "' too short and ambiguous: "
             << "[" << found->first << ", " << next_command->first
             << ", ...\n\n";
        return usage(argv[0], sout.str().c_str(), EXIT_FAILURE);
      }
      cmd = found->second;
    }
    if (cmd == Command::kNone) {
      std::stringstream sout;
      sout << "Unknown command prefix '" << cmd_string << "'\n\n";
      return usage(argv[0], sout.str().c_str(), EXIT_FAILURE);
    }
    ++optind;
  }

  if (cmd == Command::kNone) {
    return usage(argv[0], "Command expected", EXIT_FAILURE);
  }

  if (optind < argc) {
    if (auto p = BazelPattern::ParseFrom(argv[optind]); p.has_value()) {
      pattern = p.value();
    } else {
      std::stringstream sout;
      sout << "Invalid pattern " << argv[optind] << "\n\n";
      return usage(argv[0], sout.str().c_str(), EXIT_FAILURE);
    }
    ++optind;
  }

  if (optind < argc) {
    // TODO: read a list of patterns.
    std::cerr << argv[optind]
              << " Sorry, can only deal with one pattern right now\n";
    return 1;
  }

  // Don't look through everything for these.
  if (cmd == Command::kCanonicalizeDeps || cmd == Command::kDWYU ||
      cmd == Command::kPrint) {
    if (pattern.is_matchall()) {
      std::cerr << "Please provide a bazel pattern for this command.\n"
                << "Examples: //... or //foo/bar:baz\n";
      return EXIT_FAILURE;
    }
  }

  bant::Session session(primary_out, info_out, flags);

  // -- TODO: a lot of the following functionality including choosing what
  // data is needed needs to move into each command itself.
  // We don't have a 'Command' object yet, so linear here.
  auto workspace_or = bant::LoadWorkspace(session);
  if (!workspace_or.has_value()) {
    std::cerr
      << "Didn't find any workspace file. Is this a bazel project root ?\n";
    return EXIT_FAILURE;
  }
  if (kAugmentWorkspacdFromDirectoryStructure) {
    BestEffortAugmentFromExternalDir(workspace_or.value());
  }
  const bant::BazelWorkspace &workspace = workspace_or.value();

  // Has dependent needs to be able to see all the files to know everything
  // that depends on a specific pattern.
  const BazelPattern dep_pattern =
    (cmd == Command::kHasDependents) ? BazelPattern() : pattern;

  bant::ParsedProject project(workspace, flags.verbose);
  if (cmd != Command::kListWorkkspace) {
    if (project.FillFromPattern(session, dep_pattern) == 0) {
      session.error() << "Pattern did not match any dir with BUILD file.\n";
    }
  }

  if (flags.recurse_dependency_depth <= 0 &&
      (cmd == Command::kDWYU || cmd == Command::kHasDependents)) {
    flags.recurse_dependency_depth = std::numeric_limits<int>::max();
  }

  // TODO: move dependency graph creation to tools once they are
  // Command-objects.
  bant::DependencyGraph graph;
  switch (cmd) {
  case Command::kDWYU:
  case Command::kParse:
  case Command::kLibraryHeaders:
  case Command::kGenruleOutputs:
  case Command::kListTargets:
  case Command::kListPackages:
  case Command::kDependsOn:
  case Command::kHasDependents:
    if (flags.recurse_dependency_depth >= 0) {
      graph =
        bant::BuildDependencyGraph(session, workspace, dep_pattern,
                                   flags.recurse_dependency_depth, &project);
      if (session.verbose()) {
        session.info() << "Found " << graph.depends_on.size()
                       << " Targets with dependencies; "
                       << graph.has_dependents.size()
                       << " referenced by others.\n";
        // Currently, we're not using the graph yet, just use it as a way to
        // populate project.
      }
    }
    break;
  default:;
  }

  if (flags.elaborate || cmd == Command::kDWYU) {
    bant::Elaborate(session, &project);
  }

  // library headers and genrule outputs just match the pattern unless
  // recursive is chosen when we want to print everything the dependency graph
  // gathered.
  const BazelPattern print_pattern =
    flags.recurse_dependency_depth > 0 ? BazelPattern() : pattern;

  // This will be all separate commands in their own class.
  switch (cmd) {
  case Command::kPrint: flags.print_ast = true; [[fallthrough]];
  case Command::kParse:
    // Parsing has already be done by now by building the dependency graph
    if (flags.print_ast || flags.print_only_errors) {
      bant::PrintProject(pattern, *primary_out, *info_out, project,
                         flags.print_only_errors);
    }
    break;

  case Command::kLibraryHeaders:  //
    bant::PrintProvidedSources(session, "header", print_pattern,
                               ExtractHeaderToLibMapping(project, *info_out));
    break;

  case Command::kGenruleOutputs:
    bant::PrintProvidedSources(session, "generated-file", print_pattern,
                               ExtractGeneratedFromGenrule(project, *info_out));
    break;

  case Command::kDWYU:
    bant::CreateDependencyEdits(session, project, pattern,
                                CreateBuildozerDepsEditCallback(*primary_out));
    break;

  case Command::kCanonicalizeDeps:
    CreateCanonicalizeEdits(session, project, pattern,
                            CreateBuildozerDepsEditCallback(*primary_out));
    break;

  case Command::kListPackages: {
    auto printer = TablePrinter::Create(session.out(), session.output_format(),
                                        {"bazel-file", "package"});
    for (const auto &[package, parsed] : project.ParsedFiles()) {
      printer->AddRow({std::string(parsed->name()), package.ToString()});
    }
    printer->Finish();
  } break;

  case Command::kListTargets: {
    auto printer = TablePrinter::Create(session.out(), session.output_format(),
                                        {"file-location", "rule", "target"});
    for (const auto &[package, parsed] : project.ParsedFiles()) {
      FindTargets(parsed->ast, {}, [&](const Result &target) {
        auto target_name =
          BazelTarget::ParseFrom(absl::StrCat(":", target.name), package);
        if (!target_name.has_value()) {
          return;
        }
        if (!print_pattern.Match(*target_name)) return;
        printer->AddRow({project.Loc(target.name),
                         std::string(target.rule),  //
                         target_name->ToString()});
      });
    }
    printer->Finish();
  } break;

  case Command::kListWorkkspace: {
    // For now, we just load the workspace file in this command. We might need
    // it later also to resolve dependencies.
    auto printer = TablePrinter::Create(session.out(), session.output_format(),
                                        {"project", "version", "directory"});
    for (const auto &[project, file] : workspace_or->project_location) {
      printer->AddRow({project.project,
                       project.version.empty() ? "-" : project.version,
                       file.path()});
    }
    printer->Finish();
  } break;

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
    PrintOneToN(session, pattern, graph.has_dependents,  //
                "library", "has-dependent");
    break;

  case Command::kNone:  // nop (implicitly done by parsing)
    ;
  }

  if (flags.verbose) {
    // If verbose explicitly chosen, we want to print this even if -q.
    // So not to info_out, but std::cerr
    for (const std::string_view subsystem : session.stat_keys()) {
      std::cerr << subsystem << " " << *session.stat(subsystem) << "\n";
    }
  }

  return project.error_count();
}
