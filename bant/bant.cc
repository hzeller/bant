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
#include "bant/tool/canon-targets.h"
#include "bant/tool/dwyu.h"
#include "bant/tool/edit-callback.h"
#include "bant/tool/header-providers.h"
#include "bant/types-bazel.h"
#include "bant/util/resolve-packages.h"
#include "bant/workspace.h"

static int usage(const char *prog, int exit_code) {
  fprintf(stderr,
          "Copyright (c) 2024 Henner Zeller. "
          "This program is free software; license GPL 2.0.\n");
  fprintf(stderr, "Usage: %s [options] <command> [pattern]\n", prog);
  fprintf(stderr, R"(Options
    -C <directory> : Change to project directory (default = '.')
    -q             : Quiet: don't print info messages to stderr.
    -o <filename>  : Instead of stdout, emit command primary output to file.
    -v             : Verbose; print some stats.
    -h             : This help.

Commands (unique prefix sufficient):
    parse          : Parse all BUILD files from pattern the ones they depend on.
    print          : Print rules matching pattern. -e : only files with errors
    list           : List all the build files relevant for the pattern.
    workspace      : Print external projects found in WORKSPACE.
    lib-headers    : Print table header files -> libraries that define them.
    genrule-outputs: Print table generated files -> genrules creating them.
    dwyu           : DWYU: Depend on What You Use (emit buildozer edit script)
    canonicalize   : Emit rename edits to canonicalize targets.
)");
  return exit_code;
}

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
    kListBazelFiles,
    kListWorkkspace,
    kLibraryHeaders,
    kGenruleOutputs,
    kDependencyEdits,
    kCanonicalizeDeps,
  } cmd = Command::kNone;
  static const std::map<std::string_view, Command> kCommandNames = {
    {"parse", Command::kParse},
    {"print", Command::kPrint},
    {"list", Command::kListBazelFiles},
    {"workspace", Command::kListWorkkspace},
    {"lib-headers", Command::kLibraryHeaders},
    {"genrule-outputs", Command::kGenruleOutputs},
    {"dwyu", Command::kDependencyEdits},
    {"canonicalize", Command::kCanonicalizeDeps},
  };
  int opt;
  while ((opt = getopt(argc, argv, "C:qo:vhpec")) != -1) {
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

    case 'v': verbose = true; break;
    default: return usage(argv[0], EXIT_SUCCESS);
    }
  }

  if (optind < argc) {
    const std::string_view cmd_string = argv[optind];
    auto found = kCommandNames.lower_bound(cmd_string);
    if (found != kCommandNames.end() && found->first.starts_with(cmd_string)) {
      auto next_command = std::next(found);
      if (next_command != kCommandNames.end() &&
          next_command->first.starts_with(cmd_string)) {
        std::cerr << "Command '" << cmd_string << "' too short and ambiguous: "
                  << "[" << found->first << ", " << next_command->first
                  << ", ...\n\n";
        return usage(argv[0], EXIT_FAILURE);
      }
      cmd = found->second;
    }
    if (cmd == Command::kNone) {
      std::cerr << "Unknown command prefix '" << cmd_string << "'\n\n";
      return usage(argv[0], EXIT_FAILURE);
    }
    ++optind;
  }

  if (cmd == Command::kNone) {
    std::cerr << "Command expected\n\n";
    return usage(argv[0], EXIT_FAILURE);
  }

  if (optind < argc) {
    if (auto p = bant::BazelPattern::ParseFrom(argv[optind]); p.has_value()) {
      pattern = p.value();
    } else {
      std::cerr << "Invalid pattern " << argv[optind] << "\n\n";
      return usage(argv[0], EXIT_FAILURE);
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
      std::cerr << "Please provide a bazel pattern for this command.\n";
      return EXIT_FAILURE;
    }
  }

  auto workspace_or = bant::LoadWorkspace(*info_out);
  if (!workspace_or.has_value()) {
    std::cerr
      << "Didn't find any workspace file. Is this a bazel project root ?\n";
    return EXIT_FAILURE;
  }
  const bant::BazelWorkspace &workspace = workspace_or.value();

  bant::Stat file_collect_stats;
  const auto build_files =
    bant::CollectBuildFiles(workspace, pattern, file_collect_stats);

  bant::Stat deps_stat;

  // Rest of the commands need to parse the project.
  auto &parse_err_out = (cmd == Command::kParse ? *primary_out : *info_out);

  bant::ParsedProject project(verbose);
  for (const auto &build_file : build_files) {
    project.AddBuildFile(build_file, *info_out, parse_err_out);
  }

  // TODO: right now, always create depenency graph, but we only need it
  // for some commands.
  const bant::DependencyGraph graph = bant::BuildDependencyGraph(
    workspace, pattern, &project, verbose, *info_out);
  *info_out << "Found " << graph.depends_on.size()
            << " Targets with dependencies; " << graph.has_dependents.size()
            << " referenced by others.\n";

  switch (cmd) {
  case Command::kPrint: print_ast = true; [[fallthrough]];
  case Command::kParse:
    if (print_ast || print_only_errors) {
      bant::PrintProject(pattern, *primary_out, *info_out, project,
                         print_only_errors);
    }
    break;
  case Command::kLibraryHeaders:  //
    bant::PrintProvidedSources(ExtractHeaderToLibMapping(project, *info_out),
                               pattern, *primary_out);
    break;
  case Command::kGenruleOutputs:
    bant::PrintProvidedSources(ExtractGeneratedFromGenrule(project, *info_out),
                               pattern, *primary_out);
    break;
  case Command::kDependencyEdits:
    using bant::CreateBuildozerDepsEditCallback;
    bant::CreateDependencyEdits(project, pattern, deps_stat, *info_out, verbose,
                                CreateBuildozerDepsEditCallback(*primary_out));
    break;
  case Command::kCanonicalizeDeps:
    using bant::CreateCanonicalizeEdits;
    CreateCanonicalizeEdits(project, pattern, *info_out,
                            CreateBuildozerDepsEditCallback(*primary_out));
    break;
  case Command::kListBazelFiles:
    for (const auto &[filename, _] : project.ParsedFiles()) {
      *primary_out << filename << "\n";
    }
    break;
  case Command::kListWorkkspace: {
    // For now, we just load the workspace file in this command. We might need
    // it later also to resolve dependencies.
    for (const auto &[project, file] : workspace_or->project_location) {
      *primary_out << absl::StrFormat(
        "%-33s %12s %s\n", project.project,
        project.version.empty() ? "-" : project.version, file.path());
    }
  } break;
  case Command::kNone:  // nop (implicitly done by parsing)
    ;
  }

  if (verbose) {
    // If verbose explicitly chosen, we want to print this even if -q.
    // So not to info_out, but std::cerr
    std::cerr << "Walked through " << file_collect_stats.ToString("files/dirs")
              << " to collect initial BUILD files.\n"
              << "Parsed " << project.stats().ToString("BUILD files") << "; "
              << project.error_count() << " with parse issues.\n";
    if (cmd == Command::kDependencyEdits) {
      std::cerr << "Grep'd " << deps_stat.ToString("sources")
                << " to extract includes and create dependency edits.\n";
    }
  }

  return project.error_count();
}
