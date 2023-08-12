#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <vector>

#include "parser.h"

namespace fs = std::filesystem;

std::optional<std::string> ReadFileToString(const std::string &filename) {
  std::ifstream is(filename, std::ifstream::binary);
  if (!is.good()) return std::nullopt;
  std::string result;
  char buffer[4096];
  for (;;) {
    is.read(buffer, sizeof(buffer));
    result.append(buffer, is.gcount());
    if (!is.good()) break;
  }
  return result;
}

static int usage(const char *prog) {
  fprintf(stderr, "Usage: [options] %s <filename> [<filename>...]\n", prog);
  fprintf(stderr, R"(Options
	-C<directory>  : base directory
	-p             : print parse tree
	-h             : this help
)");
  return 1;
}

using FileAccept = std::function<bool(const fs::path &)>;
void CollectFilesRecursive(const fs::path &dir, bool follow_symlink_dirs,
                           std::vector<fs::path> *paths,
                           const FileAccept &include_p) {
  std::error_code err;
  if (!fs::is_directory(dir, err) || err.value() != 0) return;
  if (!follow_symlink_dirs && fs::is_symlink(dir, err)) return;

  for (const fs::directory_entry &e : fs::directory_iterator(dir)) {
    if (e.is_directory()) {
      CollectFilesRecursive(e.path(), follow_symlink_dirs, paths, include_p);
    } else if (include_p(e.path())) {
      paths->emplace_back(e.path());
    }
  }
}

int main(int argc, char *argv[]) {
  bool print_parsed = false;
  int opt;
  while ((opt = getopt(argc, argv, "C:p")) != -1) {
    switch (opt) {
    case 'C': std::filesystem::current_path(optarg); break;
    case 'p': print_parsed = true; break;
    default: return usage(argv[0]);
    }
  }

  std::vector<fs::path> build_files;
  CollectFilesRecursive(".", false, &build_files, [](const fs::path &file) {
    const auto &basename = file.filename();
    return basename == "BUILD" || basename == "BUILD.bazel";
  });

  int file_count = 0;
  int file_error_count = 0;

  Arena arena(1 << 16);
  for (const fs::path &build_file : build_files) {
    const std::string filename = build_file.u8string();
    std::optional<std::string> content = ReadFileToString(filename);
    if (!content.has_value()) {
      std::cerr << "Could not read " << filename << "\n";
      ++file_error_count;
      continue;
    }
    ++file_count;
    Scanner scanner(*content);
    Parser parser(&scanner, &arena, filename.c_str(), std::cerr);
    List *const statements = parser.parse();
    if (print_parsed && statements) {
      std::cerr << "------- file " << filename << "\n";
      PrintVisitor printer(std::cout);
      statements->Accept(&printer);
      std::cout << "\n";
    }
    if (parser.parse_error()) {
      ++file_error_count;
    }
  }

  fprintf(stderr, "Scanned %d files; %d file with issues.\n", file_count,
          file_error_count);

  return file_error_count;
}
