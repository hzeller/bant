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

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

#include "bant/frontend/project-parser.h"
#include "bant/session.h"
#include "bant/tool/canon-targets.h"
#include "bant/tool/dwyu.h"
#include "bant/tool/edit-callback.h"
#include "bant/tool/header-providers.h"
#include "bant/types-bazel.h"
#include "bant/util/dependency-graph.h"
#include "bant/util/table-printer.h"
#include "bant/workspace.h"

#define BOLD  "\033[1m"
#define RED   "\033[1;31m"
#define RESET "\033[0m"

static int usage(const char *prog, const char *message, int exit_code) {
  fprintf(stderr,
          "Copyright (c) 2024 Henner Zeller. "
          "This program is free software; license GPL 2.0.\n");
  fprintf(stderr, "Usage: %s [options] <command> [pattern]\n", prog);
  fprintf(stderr, R"(Options
    -C <directory> : Change to project directory (default = '.')
    -q             : Quiet: don't print info messages to stderr.
    -o <filename>  : Instead of stdout, emit command primary output to file.
    -f <format>    : Output format, support depends on command. One of
                   : native (default), s-expr, plist, json, csv (unique prefix ok)
    -v             : Verbose; print some stats.
    -h             : This help.

Commands (unique prefix sufficient):
    %s== Parsing ==%s
    print          : Print AST matching pattern. -e : only files w/ parse errors
    parse          : Parse all BUILD files from pattern the ones they depend on.
                     Emit parse errors. Silent otherwise: No news are good news.

    %s== Extract facts ==%s (-f: whitespace separated columns or s-expr) ==
    list-packages  : List all packages relevant for the pattern with their
                     corresponding filename. Follows dependencies.
                     → 2 column table: (package, buildfile)
    workspace      : Print external projects found in WORKSPACE.
                     → 3 column table: (project, version, path)
    lib-headers    : Print the targets for each header file in the project.
                     → 2 column table: (header-filename, cc-library-target)
    genrule-outputs: Print names of generated files and genrules creating them
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

  enum class Command {
    kNone,
    kParse,
    kPrint,  // Like parse, but we narrow with pattern
    kListPackages,
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
  while ((opt = getopt(argc, argv, "C:qo:vhpecf:")) != -1) {
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

  // -- TODO: a lot of the following functionality needs to move into each
  // command itself. We don't have a 'Command' object yet, so linear here.
  auto workspace_or = bant::LoadWorkspace(session);
  if (!workspace_or.has_value()) {
    std::cerr
      << "Didn't find any workspace file. Is this a bazel project root ?\n";
    return EXIT_FAILURE;
  }
  const bant::BazelWorkspace &workspace = workspace_or.value();

  bant::ParsedProject project(verbose);
  project.FillFromPattern(session, workspace, pattern);

  // TODO: move dependency graph creation to tools where needed.
  // Some also should also read _all_ the BUILD files from the workspace.
  if (cmd == Command::kLibraryHeaders || cmd == Command::kGenruleOutputs ||
      cmd == Command::kDependencyEdits || cmd == Command::kListPackages) {
    const bant::DependencyGraph graph =
      bant::BuildDependencyGraph(session, workspace, pattern, &project);
    session.info() << "Found " << graph.depends_on.size()
                   << " Targets with dependencies; "
                   << graph.has_dependents.size() << " referenced by others.\n";
  }

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
    bant::PrintProvidedSources(session, "header", pattern,
                               ExtractHeaderToLibMapping(project, *info_out));
    break;
  case Command::kGenruleOutputs:
    bant::PrintProvidedSources(session, "generated-file", pattern,
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
                                        {"package", "bazel-file"});
    for (const auto &[package, parsed] : project.ParsedFiles()) {
      printer->AddRow({package.ToString(), std::string(parsed->source.name())});
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
    for (std::string_view subsystem : session.stat_keys()) {
      std::cerr << subsystem << " " << *session.stat(subsystem) << "\n";
    }
  }

  return project.error_count();
}
