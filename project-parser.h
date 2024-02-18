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

#ifndef BANT_PROJECT_PARDER_
#define BANT_PROJECT_PARDER_

#include <map>
#include <ostream>
#include <string>

#include "ast.h"
#include "linecolumn-map.h"
#include "types-bazel.h"

namespace bant {
struct FileContent {
  FileContent(std::string &&c) : content(std::move(c)) {}
  FileContent(FileContent &&) = default;
  FileContent(const FileContent &) = delete;

  BazelPackage package;
  std::string content;  // AST string_views refer to this, don't change alloc
  LineColumnMap line_columns;  // To recover line/column information from Tokens
  List *ast;           // parsed AST. Content owned by arena in ParsedProject
  std::string errors;  // List of errors if observed (todo: make actual list)
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
#endif  // BANT_PROJECT_PARDER_
