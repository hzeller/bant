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

// TODO: these of course need to be configurable. Ideally with a simple
// path query language.
using FindCallback =
  std::function<void(std::string_view library_name, std::string_view header)>;
class LibraryHeaderFinder : public BaseVisitor {
 public:
  explicit LibraryHeaderFinder(const FindCallback &cb) : found_cb_(cb) {}

  void VisitFunCall(FunCall *f) final {
    current_.Reset(f->identifier()->id() == "cc_library");
    if (!current_.is_relevant) return;  // Nothing interesting beyond here.
    for (Node *element : *f->argument()) {
      if (element) element->Accept(this);
    }
    if (!current_.name.empty() && current_.header_list) {
      for (Node *header : *current_.header_list) {
        Scalar *scalar = header->CastAsScalar();
        if (!scalar) continue;
        std::string_view header_file = scalar->AsString();
        if (header_file.empty()) continue;
        found_cb_(current_.name, header_file);
      }
    }
  }

  void VisitAssignment(Assignment *a) {
    if (!current_.is_relevant) return;  // can prune walk here
    if (!a->identifier() || !a->value()) return;
    const std::string_view lhs = a->identifier()->id();
    if (Scalar *scalar = a->value()->CastAsScalar(); scalar && lhs == "name") {
      current_.name = scalar->AsString();
    } else if (List *list = a->value()->CastAsList(); list && lhs == "hdrs") {
      current_.header_list = list;
    }
  }

 private:
  struct CollectResult {
    void Reset(bool relevant) {
      is_relevant = relevant;
      name = "";
      header_list = nullptr;
    }

    bool is_relevant = false;
    std::string_view name;
    List *header_list;
  };

  // TODO: this assumes library call being a toplevel function; might need
  // stack here for more sophisticated searches.
  CollectResult current_;

  const FindCallback &found_cb_;
};

void FindCCLibraryHeaders(Node *ast, const FindCallback &cb) {
  LibraryHeaderFinder finder(cb);
  ast->Accept(&finder);
}

// Given a BUILD, BUILD.bazel filename, return the bare project path with
// no prefix or suffix.
// ./foo/bar/baz/BUILD.bazel turns into foo/bar/baz
std::string_view TargetPathFromBuileFile(std::string_view file) {
  file = file.substr(0, file.find_last_of('/'));  // Remove BUILD-file
  while (!file.empty() && (file[0] == '.' || file[0] == '/')) {
    file.remove_prefix(1);
  }
  return file;
}

void PrintProject(const ParsedProject &project, bool only_files_with_errors) {
  for (const auto &[filename, parse_result] : project.file_to_ast) {
    if (only_files_with_errors && parse_result.errors.empty()) {
      continue;
    }
    std::cerr << "------- file " << filename << "\n";
    std::cerr << parse_result.errors;
    if (!parse_result.ast) continue;
    PrintVisitor printer(std::cout);
    parse_result.ast->Accept(&printer);
    std::cout << "\n";
  }
}

using HeaderToTargetMap = std::map<std::string, std::string>;
HeaderToTargetMap ExtractHeaderToLibMapping(const ParsedProject &project) {
  HeaderToTargetMap result;
  for (const auto &[filename, parse_result] : project.file_to_ast) {
    const std::string_view target_basename(TargetPathFromBuileFile(filename));
    if (!parse_result.ast) continue;
    FindCCLibraryHeaders(parse_result.ast,
                         [target_basename, &result](std::string_view name,
                                                    std::string_view header) {
                           std::string header_fqn(target_basename);
                           std::string lib_fqn(target_basename);
                           if (!target_basename.empty()) header_fqn.append("/");
                           header_fqn.append(header);
                           lib_fqn.append(":").append(name);
                           result[header_fqn] = lib_fqn;
                         });
  }
  return result;
}

void PrintLibraryHeaders(const ParsedProject &project) {
  const auto header_to_lib = ExtractHeaderToLibMapping(project);
  int longest = 0;
  for (const auto &[header, _] : header_to_lib) {
    longest = std::max(longest, (int)header.length());
  }
  for (const auto &[header, lib] : header_to_lib) {
    fprintf(stdout, "%*s\t%s\n", -longest - 1, header.c_str(), lib.c_str());
  }
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
    PrintProject(parsed, print_only_errors);
  }

  if (print_library_headers) {
    PrintLibraryHeaders(parsed);
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
