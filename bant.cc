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
#include <iostream>

#include "project-parser.h"
#include "tool-dwyu.h"
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
	-q             : Quiet: don't print info messages to stderr.
	-v             : Verbose; print some stats.
	-h             : This help.

Commands:
	(no-flag)      : Just parse BUILD files of project, emit parse errors.
	                 Parse is primary objective, errors go to stdout.
	                 Other commands below with different main output
	                 emit errors to info stream (stderr or none if -q)
	-L             : List all the build files found in project
	-P             : Print parse tree (-e : only files with parse errors)
	-H             : Print table header files -> targets that define them.
	-D             : DWYU: Depend on What You Use (emit buildozer edits)
)");
  return 1;
}

int main(int argc, char *argv[]) {
  // '/dev/null' :)
  class NullBuf : public std::streambuf {
    std::streamsize xsputn(const char *s, std::streamsize n) final { return n; }
    int overflow(int c) final { return c; }
  } null_buf;
  std::ostream devnull{&null_buf};

  std::ostream *primary_output = &std::cout;  // TODO: could also be -o
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
  while ((opt = getopt(argc, argv, "hC:vxPeHLDq")) != -1) {
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
      info_output = &devnull;
      break;
    case 'x':
      include_external = false;
      break;

      // TODO: instead of flags, these sub-commands should be given as string
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
      *primary_output << file.string() << "\n";
    }
    if (verbose) {
      *info_output << "Walked through " << stats.ToString("files/dirs") << "\n";
    }
    return 0;
  }

  // Rest of the commands need to parse the project.
  auto &parse_err_out = cmd == Command::kNone ? *primary_output : *info_output;
  const bant::ParsedProject project = bant::ParsedProject::FromFilesystem(
    include_external, parse_err_out);

  switch (cmd) {
  case Command::kPrint:
    bant::PrintProject(*primary_output, *info_output, project,
                       print_only_errors);
    break;
  case Command::kLibraryHeaders:  //
    bant::PrintLibraryHeaders(stdout, project);
    break;
  case Command::kDependencyEdits:
    bant::EmitBuildozerDWYUEdits(project, *primary_output, *info_output);
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
              << project.error_count << " with issues\n";
  }

  return project.error_count;
}
