#include "project-parser.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "parser.h"

namespace fs = std::filesystem;

namespace bant {
static std::optional<std::string> ReadFileToString(
  const std::string &filename) {
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

void PrintProject(std::ostream &out, std::ostream &info_out,
                  const ParsedProject &project, bool only_files_with_errors) {
  for (const auto &[filename, parse_result] : project.file_to_ast) {
    if (only_files_with_errors && parse_result.errors.empty()) {
      continue;
    }
    info_out << "------- file " << filename << "\n";
    info_out << parse_result.errors;
    if (!parse_result.ast) continue;
    PrintVisitor printer(out);
    parse_result.ast->Accept(&printer);
    out << "\n";
  }
}
}  // namespace bant
