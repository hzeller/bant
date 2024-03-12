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

static int usage(const char *prog, int exit_code) {
  fprintf(stderr,
          "Copyright (c) 2024 Henner Zeller. "
          "This program is free software; license GPL 2.0.\n");
  fprintf(stderr, "Usage: %s [options] <command>\n", prog);
  fprintf(stderr, R"(Options
    -C <directory> : Change to project directory (default = '.')
    -x             : Do not read BUILD files of eXternal projects (e.g. @foo)
                     (i.e. only read the files in the direct project)
    -q             : Quiet: don't print info messages to stderr.
    -o <filename>  : Instead of stdout, emit command primary output to file.
    -v             : Verbose; print some stats.
    -h             : This help.

Commands (unique prefix sufficient):
    parse          : Just parse BUILD files of project, emit parse errors
                     (which might well be due to bant not handling that yet).
                     -p : also print abstract syntax tree (AST) for all files.
                     -e : Only for files with parse errors: print partial AST.
    list           : List all the build files found in project
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

  bool verbose = false;
  bool print_ast = false;
  bool print_only_errors = false;
  bool include_external = true;

  enum class Command {
    kNone,
    kParse,
    kListBazelFiles,
    kLibraryHeaders,
    kGenruleOutputs,
    kDependencyEdits,
    kCanonicalizeDeps,
  } cmd = Command::kNone;
  static const std::map<std::string_view, Command> kCommandNames = {
    {"parse", Command::kParse},
    {"list", Command::kListBazelFiles},
    {"lib-headers", Command::kLibraryHeaders},
    {"genrule-outputs", Command::kGenruleOutputs},
    {"dwyu", Command::kDependencyEdits},
    {"canonicalize", Command::kCanonicalizeDeps},
  };
  int opt;
  while ((opt = getopt(argc, argv, "C:xqo:vhpec")) != -1) {
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

    case 'x':
      include_external = false;
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
                  << ", ...\n";
        return usage(argv[0], EXIT_FAILURE);
      }
      cmd = found->second;
    }
    if (cmd == Command::kNone) {
      std::cerr << "Unknown command prefix '" << cmd_string << "'\n";
      return usage(argv[0], EXIT_FAILURE);
    }
    ++optind;
  }

  if (cmd == Command::kNone) {
    std::cerr << "Command expected\n";
    return usage(argv[0], EXIT_FAILURE);
  }

  bant::Stat file_collect_stats;
  auto build_files =
    bant::CollectBuildFiles(include_external, file_collect_stats);

  if (cmd == Command::kListBazelFiles) {  // This one does not parse project
    for (const auto &file : build_files) {
      *primary_out << file.path() << "\n";
    }
    if (verbose) {
      *info_out << "Walked through "
                << file_collect_stats.ToString("files/dirs") << "\n";
    }
    return 0;
  }

  bant::Stat deps_stat;

  // Rest of the commands need to parse the project.
  auto &parse_err_out = (cmd == Command::kParse ? *primary_out : *info_out);

  bant::ParsedProject project(verbose);
  for (const auto &build_file : build_files) {
    project.AddBuildFile(build_file, parse_err_out);
  }

  switch (cmd) {
  case Command::kParse:
    if (print_ast || print_only_errors) {
      bant::PrintProject(*primary_out, *info_out, project, print_only_errors);
    }
    break;
  case Command::kLibraryHeaders:  //
    bant::PrintProvidedSources(ExtractHeaderToLibMapping(project, *info_out),
                               *primary_out);
    break;
  case Command::kGenruleOutputs:
    bant::PrintProvidedSources(ExtractGeneratedFromGenrule(project, *info_out),
                               *primary_out);
    break;
  case Command::kDependencyEdits:
    using bant::CreateBuildozerDepsEditCallback;
    bant::CreateDependencyEdits(project, deps_stat, *info_out, verbose,
                                CreateBuildozerDepsEditCallback(*primary_out));
    break;
  case Command::kCanonicalizeDeps:
    using bant::CreateCanonicalizeEdits;
    CreateCanonicalizeEdits(project, *info_out,
                            CreateBuildozerDepsEditCallback(*primary_out));
    break;
  case Command::kListBazelFiles:  // already handled
  case Command::kNone:            // nop (implicitly done by parsing)
    ;
  }

  if (verbose) {
    // If verbose explicitly chosen, we want to print this even if -q.
    // So not to info_out, but std::cerr
    std::cerr << "Walked through " << file_collect_stats.ToString("files/dirs")
              << " to collect BUILD files.\n"
              << "Parsed " << project.stats().ToString("BUILD files") << "; "
              << project.error_count() << " with parse issues.\n";
    if (cmd == Command::kDependencyEdits) {
      std::cerr << "Grep'd " << deps_stat.ToString("sources")
                << " to extract includes and create dependency edits.\n";
    }
  }

  return project.error_count();
}
