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
#include "bant/tool/dwyu.h"
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
    lib-headers    : Print table header files -> targets that define them.
    dwyu           : DWYU: Depend on What You Use (emit buildozer edit script)
)");
  return exit_code;
}

int main(int argc, char *argv[]) {
  // non-nullptr streams if chosen by user.
  std::unique_ptr<std::ostream> user_primary_output;
  std::unique_ptr<std::ostream> user_info_output;

  // Default primary and info outputs.
  std::ostream *primary_output = &std::cout;
  std::ostream *info_output = &std::cerr;

  bool verbose = false;
  bool print_ast = false;
  bool print_only_errors = false;
  bool include_external = true;

  enum class Command {
    kNone,
    kParse,
    kListBazelFiles,
    kLibraryHeaders,
    kDependencyEdits,
  } cmd = Command::kNone;
  static const std::map<std::string_view, Command> kCommandNames = {
    {"parse", Command::kParse},
    {"list", Command::kListBazelFiles},
    {"lib-headers", Command::kLibraryHeaders},
    {"dwyu", Command::kDependencyEdits},
  };
  int opt;
  while ((opt = getopt(argc, argv, "C:xqo:vhpe")) != -1) {
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
      user_info_output.reset(new std::ostream(nullptr));
      info_output = user_info_output.get();
      break;

    case 'o':  //
      if (std::string_view(optarg) == "-") {
        primary_output = &std::cout;
        break;
      }
      user_primary_output.reset(new std::fstream(
        optarg, std::ios::out | std::ios::binary | std::ios::trunc));
      if (!user_primary_output->good()) {
        std::cerr << "Could not open '" << optarg << "'\n";
        return 1;
      }
      primary_output = user_primary_output.get();
      break;

    case 'x': include_external = false; break;

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

  if (cmd == Command::kListBazelFiles) {  // This one does not parse project
    bant::Stat stats;
    auto build_files = bant::CollectBuildFiles(include_external, stats);
    for (const auto &file : build_files) {
      *primary_output << file.path() << "\n";
    }
    if (verbose) {
      *info_output << "Walked through " << stats.ToString("files/dirs") << "\n";
    }
    return 0;
  }

  bant::Stat deps_stat;

  // Rest of the commands need to parse the project.
  auto &parse_err_out = cmd == Command::kParse ? *primary_output : *info_output;
  const bant::ParsedProject project =
    bant::ParsedProject::FromFilesystem(include_external, parse_err_out);
  project.arena.SetVerbose(verbose);

  switch (cmd) {
  case Command::kParse:
    if (print_ast || print_only_errors) {
      bant::PrintProject(*primary_output, *info_output, project,
                         print_only_errors);
    }
    break;
  case Command::kLibraryHeaders:  //
    bant::PrintLibraryHeaders(ExtractHeaderToLibMapping(project, *info_output),
                              *primary_output);
    break;
  case Command::kDependencyEdits:
    bant::CreateDependencyEdits(project, deps_stat, *info_output,
                                bant::CreateBuildozerPrinter(*primary_output));
    break;
  case Command::kListBazelFiles:  // already handled
  case Command::kNone:;           // nop (implicitly done by parsing)
  }

  if (verbose) {
    // If verbose explicitly chosen, we want to print this even if -q.
    // So not to info_output, but std::cerr
    std::cerr << "Walked through "
              << project.file_collect_stat.ToString("files/dirs")
              << " to collect BUILD files.\n"
              << "Parsed " << project.parse_stat.ToString("BUILD files") << "; "
              << project.error_count << " with parse issues.\n";
    if (cmd == Command::kDependencyEdits) {
      std::cerr << "Grep'd " << deps_stat.ToString("sources")
                << " to extract includes and create dependency edits.\n";
    }
  }

  return project.error_count;
}
