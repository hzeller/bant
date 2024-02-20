// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#ifndef BANT_PROJECT_PARDER_
#define BANT_PROJECT_PARDER_

#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "ast.h"
#include "linecolumn-map.h"
#include "types-bazel.h"

namespace bant {
struct FileContent {
  explicit FileContent(std::string_view filename, std::string &&c)
      : filename(filename), content(std::move(c)) {}
  FileContent(FileContent &&) noexcept = default;
  FileContent(const FileContent &) = delete;

  // TODO: maybe combine filename, content and line_columns
  std::string filename;
  std::string content;  // AST string_views refer to this, don't change alloc
  LineColumnMap line_columns;  // To recover line/column information from Tokens

  BazelPackage package;
  List *ast;           // parsed AST. Content owned by arena in ParsedProject
  std::string errors;  // List of errors if observed (todo: make actual list)
};

struct Stat {
  int count = 0;
  int duration_usec = 0;
  std::optional<size_t> bytes_processed;

  // Print readable string with "thing_name" used to describe the count.
  std::string ToString(std::string_view thing_name) const;
};

struct ParsedProject {
  // Parse project from the current directory. Looks for any
  // BUILD and BUILD.bazel files for the main project '//' as well as
  // all bazel-${projectname}/external/* sub-projects.
  static ParsedProject FromFilesystem(bool include_external,
                                      std::ostream &error_out);

  ParsedProject() = default;
  ParsedProject(ParsedProject &&) noexcept = default;
  ParsedProject(const ParsedProject &) = delete;

  // Some stats.
  Stat file_collect_stat;
  Stat parse_stat;

  int error_count = 0;

  Arena arena{1 << 16};
  std::map<std::string, FileContent> file_to_ast;
};

// Conveenience function to just collect all the BUILD files. Update "stats"
// with total files searched and total time.
std::vector<std::filesystem::path> CollectBuildFiles(bool include_external,
                                                     Stat &stats);

// Convenience function to print a fully parsed project, recreated from the
// AST.
// "out" is the destination of the acutal parse tree, "info_out" will
// print error message and filenames.
// If "only_files_with_errors" is set, prints only the files that had issues.
void PrintProject(std::ostream &out, std::ostream &info_out,
                  const ParsedProject &project, bool only_files_with_errors);
}  // namespace bant
#endif  // BANT_PROJECT_PARDER_
