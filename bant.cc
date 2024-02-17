#include <unistd.h>

#include <filesystem>

#include "project-parser.h"
#include "tool-header-providers.h"

static int usage(const char *prog) {
  fprintf(stderr, "Usage: [options] %s <filename> [<filename>...]\n", prog);
  fprintf(stderr, R"(Options
	-C<directory>  : project base directory (default: current dir)
	-x             : Don't read external projects
	-p             : print parse tree
	-E             : print only parse trees of files with parse errors
	-h             : this help
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
  std::error_code err;
  while ((opt = getopt(argc, argv, "C:pEHvx")) != -1) {
    switch (opt) {
    case 'C': {
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
  const ParsedProject parsed = ParsedProject::FromFilesystem(include_external);

  if (print_parsed || print_only_errors) {
    PrintProject(std::cout, std::cerr, parsed, print_only_errors);
  }

  if (print_library_headers) {
    PrintLibraryHeaders(stdout, parsed);
  }

  if (verbose) {
    fprintf(stderr,
            "Walked through %d files to find BUILD files in %.3fms.\n"
            "Parsed %d BUILD files with %.2f KiB in %.3fms (%.2f MB/sec); "
            "%d file with issues.\n",
            parsed.files_searched, parsed.file_walk_duration_usec / 1000.0,
            parsed.build_file_count, parsed.total_content_size / 1024.0,
            parsed.parse_duration_usec / 1000.0,
            1.0f * parsed.total_content_size / parsed.parse_duration_usec,
            parsed.error_count);
  }

  return parsed.error_count;
}
