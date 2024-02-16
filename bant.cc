#include <unistd.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <vector>

#include "project-parser.h"
#include "tool-header-providers.h"

namespace fs = std::filesystem;

using namespace bant;

static int usage(const char *prog) {
  fprintf(stderr, "Usage: [options] %s <filename> [<filename>...]\n", prog);
  fprintf(stderr, R"(Options
	-C<directory>  : project base directory
	-L             : follow directories that are symbolic links (many!)
	-p             : print parse tree
	-e             : print only parse trees of files with parse errors
	-h             : this help
)");
  return 1;
}

// Collect files found recursively and store in "paths". Use predicate
// "include_p" to test if file should be included in list.
// Returns number of files it looked at.
using FileAccept = std::function<bool(const fs::path &)>;
size_t CollectFilesRecursive(const fs::path &dir, bool follow_symlink_dirs,
                             std::vector<fs::path> *paths,
                             const FileAccept &include_p) {
  std::error_code err;
  if (!fs::is_directory(dir, err) || err.value() != 0) return 0;
  if (!follow_symlink_dirs && fs::is_symlink(dir, err)) return 0;

  size_t count = 0;
  for (const fs::directory_entry &e : fs::directory_iterator(dir)) {
    ++count;
    if (e.is_directory()) {
      count +=
        CollectFilesRecursive(e.path(), follow_symlink_dirs, paths, include_p);
    } else if (include_p(e.path())) {
      paths->emplace_back(e.path());
    }
  }
  return count;
}

int main(int argc, char *argv[]) {
  bool verbose = false;
  bool print_parsed = false;
  bool follow_symbolic_links = false;
  bool print_only_errors = false;
  bool print_library_headers = false;

  int opt;
  std::error_code err;
  while ((opt = getopt(argc, argv, "C:peLHv")) != -1) {
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
    case 'e': print_only_errors = true; break;
    case 'L': follow_symbolic_links = true; break;
    case 'H': print_library_headers = true; break;
    case 'v': verbose = true; break;
    default: return usage(argv[0]);
    }
  }

  std::vector<fs::path> build_files;
  const size_t search_count = CollectFilesRecursive(
    ".", follow_symbolic_links, &build_files, [](const fs::path &file) {
      const auto &basename = file.filename();
      return basename == "BUILD" || basename == "BUILD.bazel";
    });

  ParsedProject parsed = ParseBuildFiles(build_files);

  if (print_parsed || print_only_errors) {
    PrintProject(std::cout, std::cerr, parsed, print_only_errors);
  }

  if (print_library_headers) {
    PrintLibraryHeaders(stdout, parsed);
  }

  if (verbose) {
    fprintf(stderr,
            "Parsed %d files with %ld bytes in %.3fms (%.2f MB/sec); "
            "%d file with issues.\n"
            "Searched %ld files and directories to find the BUILD files.\n",
            parsed.file_count, parsed.total_content_size,
            parsed.parse_duration_usec / 1000.0,
            1.0f * parsed.total_content_size / parsed.parse_duration_usec,
            parsed.error_count, search_count);
  }

  return parsed.error_count;
}
