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

#include <ostream>
#include <string>

#include "bant/frontend/ast.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/source-locator.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/util/disjoint-range-map.h"
#include "bant/util/file-utils.h"
#include "bant/workspace.h"

namespace bant {

class ParsedBuildFile {
 public:
  ParsedBuildFile(std::string_view filename, std::string c)
      : content_(std::move(c)), source_(filename, content_) {}

  // Can't be copied or moved as AST nodes can contain string_views
  // owned by content which must not change address (even move'ing content
  // can be problematic due to small string optimization).
  ParsedBuildFile(ParsedBuildFile &&) = delete;
  ParsedBuildFile(const ParsedBuildFile &) = delete;

  std::string_view name() const { return source_.source_name(); }

  BazelPackage package;
  List *ast;           // parsed AST. Content owned by arena in ParsedProject
  std::string errors;  // List of errors if observed (todo: make actual list)

 private:
  friend class ParsedProject;  // It is allowed to access source_ directly.
  const std::string content_;
  NamedLineIndexedContent source_;  // SourceLocator: always vis ParsedProject
};

// A Parsed project contains all the parsed BUILD-files of a project.
class ParsedProject {
 public:
  using Package2Parsed =
    OneToOne<BazelPackage, std::unique_ptr<ParsedBuildFile>>;

  explicit ParsedProject(bool verbose);

  // Given a BazelPattern, collect all the matching BUILD files and add to
  // project.
  // Returns number of build-files added.
  int FillFromPattern(Session &session, const BazelWorkspace &workspace,
                      const BazelPattern &pattern);

  // Parse build file for given package reading from filename.
  const ParsedBuildFile *AddBuildFile(Session &session,
                                      const FilesystemPath &build_file,
                                      const BazelPackage &package);

  // A map of Package -> ParsedBuildFile
  const Package2Parsed &ParsedFiles() const { return package_to_parsed_; }

  // Look up parse file given the package, or nullptr, if not parsed (yet).
  const ParsedBuildFile *FindParsedOrNull(const BazelPackage &package) const;

  // Given the string_view of any content of any of the BUILD files we parsed,
  // print the <file>:<line>:<col> location of that string view to stream;
  // Must only be called with valid ranges.
  std::ostream &Loc(std::ostream &out, std::string_view s) const;

  // Same, but instead of writing to stream, returning a string.
  std::string Loc(std::string_view s) const;

  // Some stats.
  int error_count() const { return error_count_; }

  Arena *arena() { return &arena_; }

 private:
  friend class ParsedProjectTestUtil;

  // like AddBuildFile(..package), but extract package from (workspace, path).
  // TODO: should not be needed, just an artifact of FillFromPattern() workings.
  const ParsedBuildFile *AddBuildFile(Session &session,
                                      const FilesystemPath &build_file,
                                      const BazelWorkspace &workspace,
                                      std::string_view project);

  // Given package and content, parse. Main workhorse. Content is std::move()'d
  // thus by value.
  const ParsedBuildFile *AddBuildFileContent(SessionStreams &message_out,
                                             const BazelPackage &package,
                                             std::string_view filename,
                                             std::string content);

  int error_count_ = 0;
  Arena arena_{1 << 20};
  Package2Parsed package_to_parsed_;
  DisjointRangeMap<std::string_view, const SourceLocator *> location_maps_;
};

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
