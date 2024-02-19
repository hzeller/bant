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

#include "project-parser.h"
#include "tool-header-providers.h"

static int usage(const char *prog) {
  fprintf(stderr,
          "Copyright (c) 2024 Henner Zeller. "
          "This program is free software; license GPL 2.0.\n");
  fprintf(stderr, "Usage: %s [options]\n", prog);
  fprintf(stderr, R"(Options
	-C<directory>  : Project base directory (default: current dir = '.')
	-x             : Do not read BUILD files of eXternal projects.
	                 (i.e. only read the files in the direct project)
	-v             : Verbose; print some stats.
	-h             : This help.

Commands:
	(no-flag)      : Just parse BUILD files of project, emit parse errors.
	                 Parse is primary objective, errors go to stdout.
	                 Other commands below with different main output
	                 emit errors to stderr.
	-L             : List all the build files found in project
	-P             : Print parse tree (-e : only files with parse errors)
	-H             : Print table header files -> targets that define them.
)");
  return 1;
}

int main(int argc, char *argv[]) {
  bool verbose = false;
  bool print_only_errors = false;
  bool include_external = true;

  enum class Command {
    kNone,
    kPrint,
    kLibraryHeaders,
    kListBazelFiles,
  } cmd = Command::kNone;

  int opt;
  while ((opt = getopt(argc, argv, "hC:vxPeHL")) != -1) {
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
    case 'x':
      include_external = false;
      break;

      // TODO: instead of flags, these sub-commands should be given as string
    case 'P': cmd = Command::kPrint; break;
    case 'e': print_only_errors = true; break;  // command should handle

    case 'H': cmd = Command::kLibraryHeaders; break;
    case 'L': cmd = Command::kListBazelFiles; break;
    case 'v': verbose = true; break;
    default: return usage(argv[0]);
    }
  }

  std::ostream &primary_output = std::cout;  // TODO: could also be -o
  std::ostream &info_output = std::cerr;

  if (cmd == Command::kListBazelFiles) {  // This one does not parse project
    bant::Stat stats;
    auto build_files = bant::CollectBuildFiles(include_external, stats);
    for (const auto &file : build_files) {
      primary_output << file.string() << "\n";
    }
    if (verbose) {
      info_output << "Walked through " << stats.ToString("files/dirs") << "\n";
    }
    return 0;
  }

  // Rest of the commands need to parse the project.
  using bant::ParsedProject;
  using bant::PrintLibraryHeaders;
  using bant::PrintProject;

  const ParsedProject project = ParsedProject::FromFilesystem(
    include_external, cmd == Command::kNone ? primary_output : info_output);

  switch (cmd) {
  case Command::kPrint:
    PrintProject(primary_output, info_output, project, print_only_errors);
    break;
  case Command::kLibraryHeaders: PrintLibraryHeaders(stdout, project); break;
  case Command::kListBazelFiles:  // already handled
  case Command::kNone:;           // nop
  }

  if (verbose) {
    info_output << "Walked through "
                << project.file_collect_stat.ToString("files/dirs")
                << " to collect BUILD files.\n"
                << "Parsed " << project.parse_stat.ToString("BUILD files")
                << "; " << project.error_count << " with issues\n";
  }

  return project.error_count;
}
