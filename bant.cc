// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

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
	-C<directory>  : Project base directory (default: current dir)
	-x             : Do not read BUILD files of eXternal projects.
	-p             : Print parse tree.
	-E             : Print only parse trees of files with parse errors.
	-v             : Verbose; print some stats.
	-h             : This help.
)");
  return 1;
}

int main(int argc, char *argv[]) {
  bool verbose = false;
  bool print_parsed = false;
  bool print_only_errors = false;
  bool print_library_headers = false;
  bool include_external = true;

  int opt;
  while ((opt = getopt(argc, argv, "hC:pEHvx")) != -1) {
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
    case 'p': print_parsed = true; break;
    case 'E': print_only_errors = true; break;
    case 'x': include_external = false; break;
    case 'H': print_library_headers = true; break;
    case 'v': verbose = true; break;
    default: return usage(argv[0]);
    }
  }

  using namespace bant;
  const ParsedProject project = ParsedProject::FromFilesystem(include_external);

  if (print_parsed || print_only_errors) {
    PrintProject(std::cout, std::cerr, project, print_only_errors);
  }

  if (print_library_headers) {
    PrintLibraryHeaders(stdout, project);
  }

  if (verbose) {
    fprintf(stderr,
            "Walked through %d files/dirs in %.3fms to collect BUILD files.\n"
            "Parsed %d BUILD files with %.2f KiB in %.3fms (%.2f MB/sec); "
            "%d of them with issues.\n",
            project.files_searched, project.file_walk_duration_usec / 1000.0,
            project.build_file_count, project.total_content_size / 1024.0,
            project.parse_duration_usec / 1000.0,
            1.0f * project.total_content_size / project.parse_duration_usec,
            project.error_count);
  }

  return project.error_count;
}
