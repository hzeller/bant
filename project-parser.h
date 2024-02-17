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

  std::string project;   // Something like '//' or `@foo_bar//`
  std::string rel_path;  // path within that foo/bar/baz
  std::string content;   // AST string_views refer to this, don't change alloc
  List *ast;             // parsed AST. Content owned by arena in ParsedProject
  std::string errors;    // List of errors if observed (todo: make actual list)
};

struct ParsedProject {
  // Parse project from the current directory. Looks for any
  // BUILD and BUILD.bazel files for the main project '//' as well as
  // all bazel-${projectname}/external/* sub-projects.
  static ParsedProject FromFilesystem(bool include_subprojects);

  ParsedProject() = default;
  ParsedProject(ParsedProject &&) = default;
  ParsedProject(const ParsedProject &) = delete;

  // Some stats.
  int files_searched = 0;

  int build_file_count = 0;
  int error_count = 0;
  size_t total_content_size = 0;

  int file_walk_duration_usec = 0;
  int parse_duration_usec = 0;

  Arena arena{1 << 16};
  std::map<std::string, FileContent> file_to_ast;
};

// Convenience function to print a fully parsed project, recreated from the
// AST.
// "out" is the destination of the acutal parse tree, "info_out" will
// print error message and filenames.
// If "only_files_with_errors" is set, prints only the files that had issues.
void PrintProject(std::ostream &out, std::ostream &info_out,
                  const ParsedProject &project, bool only_files_with_errors);
}  // namespace bant
