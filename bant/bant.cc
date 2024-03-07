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

#include <filesystem>
#include <fstream>
#include <iostream>

#include "bant/frontend/project-parser.h"
#include "bant/tool/dwyu.h"
#include "bant/tool/header-providers.h"

static int usage(const char *prog) {
  fprintf(stderr,
          "Copyright (c) 2024 Henner Zeller. "
          "This program is free software; license GPL 2.0.\n");
  fprintf(stderr, "Usage: %s [options]\n", prog);
  fprintf(stderr, R"(Options
	-C<directory>  : Change to project directory (default = '.')
	-x             : Do not read BUILD files of eXternal projects.
	                 (i.e. only read the files in the direct project)
	-q             : Quiet: don't print info messages to stderr.
	-o <filename>  : Instead of stdout, emit command output to file.
	-v             : Verbose; print some stats.
	-h             : This help.

Commands:
	(no-flag)      : Just parse BUILD files of project, emit parse errors.
	                 Parse is primary objective, errors go to stdout.
	                 Other commands below with different main output
	                 emit errors to info stream (stderr or muted with -q)
	-L             : List all the build files found in project
	-P             : Print parse tree (-e : only files with parse errors)
	-H             : Print table header files -> targets that define them.
	-D             : DWYU: Depend on What You Use (emit buildozer edits)
)");
  return 1;
}

int main(int argc, char *argv[]) {
  // non-nullptr streams if chosen by user.
  std::unique_ptr<std::ostream> user_primary_output;
  std::unique_ptr<std::ostream> user_info_output;

  // Default primary and info outputs.
  std::ostream *primary_output = &std::cout;
  std::ostream *info_output = &std::cerr;

  bool verbose = false;
  bool print_only_errors = false;
  bool include_external = true;

  enum class Command {
    kNone,
    kPrint,
    kLibraryHeaders,
    kListBazelFiles,
    kDependencyEdits,
  } cmd = Command::kNone;

  int opt;
  while ((opt = getopt(argc, argv, "hC:vxPeHLDqo:")) != -1) {
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

    case 'x':
      include_external = false;
      break;

      // TODO: instead of flags, these sub-commands should be given by name
    case 'P': cmd = Command::kPrint; break;
    case 'e': print_only_errors = true; break;  // command should handle

    case 'H': cmd = Command::kLibraryHeaders; break;
    case 'L': cmd = Command::kListBazelFiles; break;
    case 'D': cmd = Command::kDependencyEdits; break;
    case 'v': verbose = true; break;
    default: return usage(argv[0]);
    }
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
  auto &parse_err_out = cmd == Command::kNone ? *primary_output : *info_output;
  const bant::ParsedProject project =
    bant::ParsedProject::FromFilesystem(include_external, parse_err_out);
  project.arena.SetVerbose(verbose);

  switch (cmd) {
  case Command::kPrint:
    bant::PrintProject(*primary_output, *info_output, project,
                       print_only_errors);
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
