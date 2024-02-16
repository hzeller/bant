#pragma once

#include <filesystem>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "ast.h"

namespace bant {
struct FileContent {
  FileContent(std::string &&c) : content(std::move(c)) {}
  FileContent(FileContent &&) = default;
  FileContent(const FileContent &) = delete;

  std::string content;  // AST string_views refer to this, must not change alloc
  List *ast;            // parsed AST. Content owned by arena in ParsedProject
  std::string errors;   // List of errors if observed (todo: make actual list)
};

struct ParsedProject {
  ParsedProject() = default;
  ParsedProject(const FileContent &) = delete;

  // Some stats.
  int file_count = 0;
  int error_count = 0;
  int parse_duration_usec = 0;
  size_t total_content_size = 0;

  Arena arena{1 << 16};
  std::map<std::string, FileContent> file_to_ast;
};

// Given a list of BUILD files, parse these and return a parsed project.
ParsedProject ParseBuildFiles(
  const std::vector<std::filesystem::path> &build_files);

// Convenience function to print a fully parsed project, recreated from the
// AST.
// "out" is the destination of the acutal parse tree, "info_out" will
// print error message and filenames.
// If "only_files_with_errors" is set, prints only the files that had issues.
void PrintProject(std::ostream &out, std::ostream &info_out,
                  const ParsedProject &project, bool only_files_with_errors);
}  // namespace bant
