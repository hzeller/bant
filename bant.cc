#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
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
	-C<directory>  : project base directory
	-L             : follow directories that are symbolic links (many!)
	-p             : print parse tree
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

struct FileContent {
  FileContent(std::string &&c) : content(std::move(c)) {}
  std::string content;  // AST refers to this
  List *ast;
  std::string errors;
};

struct ParsedProject {
  // Some stats.
  int file_count = 0;
  int error_count = 0;
  int parse_duration_usec = 0;
  size_t total_content_size = 0;

  Arena arena{1 << 16};
  std::map<std::string, FileContent> file_to_ast;
};

ParsedProject ParseBuildFiles(const std::vector<fs::path> &build_files) {
  ParsedProject result;

  const auto start_time = std::chrono::system_clock::now();

  for (const fs::path &build_file : build_files) {
    const std::string filename = build_file.u8string();
    std::optional<std::string> content = ReadFileToString(filename);
    if (!content.has_value()) {
      std::cerr << "Could not read " << filename << "\n";
      ++result.error_count;
      continue;
    }

    auto inserted =
      result.file_to_ast.emplace(filename, FileContent(std::move(*content)));
    if (!inserted.second) {
      std::cerr << "Already seen " << filename << "\n";
      continue;
    }

    FileContent &parse_result = inserted.first->second;
    ++result.file_count;
    result.total_content_size += parse_result.content.size();

    Scanner scanner(parse_result.content);
    std::stringstream error_collect;
    Parser parser(&scanner, &result.arena, filename.c_str(), error_collect);
    parse_result.ast = parser.parse();
    parse_result.errors = error_collect.str();
    if (parser.parse_error()) {
      std::cerr << error_collect.str();
      ++result.error_count;
    }
  }

  // fill FYI field.
  const auto end_time = std::chrono::system_clock::now();
  result.parse_duration_usec =
    std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time)
      .count();

  return result;
}

void PrintProject(const ParsedProject &project) {
  for (const auto &[filename, parse_result] : project.file_to_ast) {
    std::cerr << "------- file " << filename << "\n";
    std::cerr << parse_result.errors;
    if (!parse_result.ast) continue;
    PrintVisitor printer(std::cout);
    parse_result.ast->Accept(&printer);
    std::cout << "\n";
  }
}

int main(int argc, char *argv[]) {
  bool print_parsed = false;
  bool follow_symbolic_links = false;
  int opt;
  while ((opt = getopt(argc, argv, "C:pL")) != -1) {
    switch (opt) {
    case 'C': std::filesystem::current_path(optarg); break;
    case 'p': print_parsed = true; break;
    case 'L': follow_symbolic_links = true; break;
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

  if (print_parsed) PrintProject(parsed);

  fprintf(stderr,
          "Parsed %d files with %ld bytes in %.3fms (%.2f MB/sec); "
          "%d file with issues.\n"
          "Searched %ld files and directories to find the BUILD files.\n",
          parsed.file_count, parsed.total_content_size,
          parsed.parse_duration_usec / 1000.0,
          1.0f * parsed.total_content_size / parsed.parse_duration_usec,
          parsed.error_count, search_count);

  return parsed.error_count;
}
