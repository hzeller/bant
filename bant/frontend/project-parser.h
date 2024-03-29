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

#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "bant/frontend/ast.h"
#include "bant/frontend/named-content.h"
#include "bant/types-bazel.h"
#include "bant/util/file-utils.h"
#include "bant/util/stat.h"

namespace bant {

struct ParsedBuildFile {
  ParsedBuildFile(std::string_view filename, std::string c)
      : content(std::move(c)), source(filename, content) {}

  // Can't be copied or moved as AST nodes can contain string_views
  // owned by content.
  ParsedBuildFile(ParsedBuildFile &&) = delete;
  ParsedBuildFile(const ParsedBuildFile &) = delete;

  std::string content;
  NamedLineIndexedContent source;

  BazelPackage package;
  List *ast;           // parsed AST. Content owned by arena in ParsedProject
  std::string errors;  // List of errors if observed (todo: make actual list)
};

// A Parsed project contains all the parsed files of a project.
class ParsedProject {
 public:
  using File2Parsed = std::map<std::string, std::unique_ptr<ParsedBuildFile>>;

  ParsedProject(bool verbose);

  // Parse build file for given package.
  const ParsedBuildFile *AddBuildFile(const FilesystemPath &build_file,
                                      const BazelPackage &package,
                                      std::ostream &info_out,
                                      std::ostream &error_out);

  // Same, auto-determine path (todo: should probably be deprecated)
  const ParsedBuildFile *AddBuildFile(const FilesystemPath &build_file,
                                      std::ostream &info_out,
                                      std::ostream &error_out);

  // A map of filename -> ParsedBuildFile
  const File2Parsed &ParsedFiles() const { return file_to_parsed_; }

  // Some stats.
  int error_count() const { return error_count_; }
  const Stat &stats() const { return parse_stat_; }

 private:
  const std::string external_prefix_;

  // Some stats.
  Stat parse_stat_;
  int error_count_ = 0;

  Arena arena_{1 << 20};
  File2Parsed file_to_parsed_;
};

// Convenience function to just collect all the BUILD files. Update "stats"
// with total files searched and total time.
std::vector<FilesystemPath> CollectBuildFiles(const BazelPattern &pattern,
                                              Stat &stats);

// Convenience function to print a fully parsed project, recreated from the
// AST.
// "out" is the destination of the acutal parse tree, "info_out" will
// print error message and filenames.
// If "only_files_with_errors" is set, prints only the files that had issues.
void PrintProject(const BazelPattern &pattern, std::ostream &out,
                  std::ostream &info_out, const ParsedProject &project,
                  bool only_files_with_errors);
}  // namespace bant
#endif  // BANT_PROJECT_PARDER_
