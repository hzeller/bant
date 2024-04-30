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

#include "absl/strings/str_cat.h"
#include "bant/explore/dependency-graph.h"
#include "bant/explore/header-providers.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/output-format.h"
#include "bant/session.h"
#include "bant/tool/canon-targets.h"
#include "bant/tool/dwyu.h"
#include "bant/tool/edit-callback.h"
#include "bant/types-bazel.h"
#include "bant/util/table-printer.h"
#include "bant/workspace.h"

#define BOLD  "\033[1m"
#define RED   "\033[1;31m"
#define RESET "\033[0m"

static int usage(const char *prog, const char *message, int exit_code) {
  fprintf(stderr,
          "Copyright (c) 2024 Henner Zeller. "
          "This program is free software; license GPL 2.0.\n");
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

// TODO: this is getting big, time to put Commands into their own objects.
int main(int argc, char *argv[]) {
  // non-nullptr streams if chosen by user.
  std::unique_ptr<std::ostream> user_primary_out;
  std::unique_ptr<std::ostream> user_info_out;

  // Default primary and info outputs.
  std::ostream *primary_out = &std::cout;
  std::ostream *info_out = &std::cerr;

  bant::BazelPattern pattern;

  bool verbose = false;
  bool print_ast = false;
  bool print_only_errors = false;
  int recurse_dependency_depth = 0;

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
    kGenruleOutputs,
    kDependencyEdits,
    kCanonicalizeDeps,
  } cmd = Command::kNone;
  static const std::map<std::string_view, Command> kCommandNames = {
    {"parse", Command::kParse},
    {"print", Command::kPrint},
    {"list-packages", Command::kListPackages},
    {"list-targets", Command::kListTargets},
    {"workspace", Command::kListWorkkspace},
    {"lib-headers", Command::kLibraryHeaders},
    {"genrule-outputs", Command::kGenruleOutputs},
    {"dwyu", Command::kDependencyEdits},
    {"canonicalize", Command::kCanonicalizeDeps},
  };
  using bant::OutputFormat;
  static const std::map<std::string_view, OutputFormat> kFormatOutNames = {
    {"native", OutputFormat::kNative}, {"s-expr", OutputFormat::kSExpr},
    {"plist", OutputFormat::kPList},   {"csv", OutputFormat::kCSV},
    {"json", OutputFormat::kJSON},     {"graphviz", OutputFormat::kGraphviz},
  };
  using bant::TablePrinter;
  OutputFormat out_fmt = OutputFormat::kNative;
  int opt;
  while ((opt = getopt(argc, argv, "C:qo:vhpecf:r::")) != -1) {
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
      recurse_dependency_depth = optarg  //
                                   ? atoi(optarg)
                                   : std::numeric_limits<int>::max();
      break;

      // "print" options
    case 'p': print_ast = true; break;
    case 'e': print_only_errors = true; break;
    case 'f': {
      auto found = kFormatOutNames.lower_bound(optarg);
      if (found == kFormatOutNames.end() || !found->first.starts_with(optarg)) {
        return usage(argv[0], "invalid -f format", EXIT_FAILURE);
      }
      out_fmt = found->second;
    } break;
    case 'v': verbose = true; break;
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
    if (auto p = bant::BazelPattern::ParseFrom(argv[optind]); p.has_value()) {
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
  if (cmd == Command::kCanonicalizeDeps || cmd == Command::kDependencyEdits ||
      cmd == Command::kPrint) {
    if (pattern.is_matchall()) {
      std::cerr << "Please provide a bazel pattern for this command.\n"
                << "Examples: //... or //foo/bar:baz\n";
      return EXIT_FAILURE;
    }
  }

  bant::Session session(primary_out, info_out, verbose, out_fmt);

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

  bant::ParsedProject project(verbose);
  if (cmd != Command::kListWorkkspace) {
    if (project.FillFromPattern(session, workspace, pattern) == 0) {
      session.error() << "Pattern did not match any dir with BUILD file.\n";
    }
  }

  if (recurse_dependency_depth <= 0 && cmd == Command::kDependencyEdits) {
    recurse_dependency_depth = std::numeric_limits<int>::max();
  }

  // TODO: move dependency graph creation to tools once they are
  // Command-objects.
  switch (cmd) {
  case Command::kDependencyEdits:
  case Command::kParse:
  case Command::kLibraryHeaders:
  case Command::kGenruleOutputs:
  case Command::kListTargets:
  case Command::kListPackages:
    if (recurse_dependency_depth > 0) {
      const bant::DependencyGraph graph = bant::BuildDependencyGraph(
        session, workspace, pattern, recurse_dependency_depth, &project);
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

  // library headers and genrule outputs just match the pattern unless
  // recursive is chosen when we want to print everything the dependency graph
  // gathered.
  const bant::BazelPattern print_pattern =
    recurse_dependency_depth > 0 ? bant::BazelPattern() : pattern;

  switch (cmd) {
  case Command::kPrint: print_ast = true; [[fallthrough]];
  case Command::kParse:
    // Parsing has already be done by now by building the dependency graph
    if (print_ast || print_only_errors) {
      bant::PrintProject(pattern, *primary_out, *info_out, project,
                         print_only_errors);
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

  case Command::kDependencyEdits:
    using bant::CreateBuildozerDepsEditCallback;
    bant::CreateDependencyEdits(session, project, pattern,
                                CreateBuildozerDepsEditCallback(*primary_out));
    break;

  case Command::kCanonicalizeDeps:
    using bant::CreateCanonicalizeEdits;
    CreateCanonicalizeEdits(session, project, pattern,
                            CreateBuildozerDepsEditCallback(*primary_out));
    break;

  case Command::kListPackages: {
    auto printer = TablePrinter::Create(session.out(), session.output_format(),
                                        {"bazel-file", "package"});
    for (const auto &[package, parsed] : project.ParsedFiles()) {
      printer->AddRow({std::string(parsed->source.name()), package.ToString()});
    }
    printer->Finish();
  } break;

  case Command::kListTargets: {
    using bant::query::FindTargets;
    using bant::query::Result;
    auto printer = TablePrinter::Create(session.out(), session.output_format(),
                                        {"file-location", "rule", "target"});
    for (const auto &[package, parsed] : project.ParsedFiles()) {
      FindTargets(parsed->ast, {}, [&](const Result &target) {
        auto target_name =
          bant::BazelTarget::ParseFrom(absl::StrCat(":", target.name), package);
        if (!target_name.has_value()) {
          return;
        }
        if (!print_pattern.Match(*target_name)) return;
        printer->AddRow({parsed->source.Loc(target.name),
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

  case Command::kNone:  // nop (implicitly done by parsing)
    ;
  }

  if (verbose) {
    // If verbose explicitly chosen, we want to print this even if -q.
    // So not to info_out, but std::cerr
    for (const std::string_view subsystem : session.stat_keys()) {
      std::cerr << subsystem << " " << *session.stat(subsystem) << "\n";
    }
  }

  return project.error_count();
}
