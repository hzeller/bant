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

#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "bant/frontend/ast.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/source-locator.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/types.h"
#include "bant/util/arena.h"
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

  // NOLINTBEGIN(misc-non-private-member-variables-in-classes) // TODO: fix
  BazelPackage package;
  List *ast;           // parsed AST. Content owned by arena in ParsedProject
  std::string errors;  // List of errors if observed (todo: make actual list)
  // NOLINTEND(misc-non-private-member-variables-in-classes)

 private:
  friend class ParsedProject;  // It is allowed to access source_ directly.
  const std::string content_;
  NamedLineIndexedContent source_;  // SourceLocator: always vis ParsedProject
};

// A Parsed project contains all the parsed BUILD-files of a project.
class ParsedProject : public SourceLocator {
 public:
  using Package2Parsed =
    OneToOne<BazelPackage, std::unique_ptr<ParsedBuildFile>>;

  ParsedProject(BazelWorkspace workspace, bool verbose);

  // Given a BazelPattern, collect all the matching BUILD files and add to
  // project.
  // Returns number of build-files added.
  int FillFromPattern(Session &session, const BazelPattern &pattern);

  // Parse build file for given package reading from filename.
  ParsedBuildFile *AddBuildFile(Session &session,
                                const FilesystemPath &build_file,
                                const BazelPackage &package);

  // A map of Package -> ParsedBuildFile
  const Package2Parsed &ParsedFiles() const { return package_to_parsed_; }

  // Look up parse file given the package, or nullptr, if not parsed (yet).
  const ParsedBuildFile *FindParsedOrNull(const BazelPackage &package) const;

  // Some stats.
  int error_count() const { return error_count_; }

  // Arena all Nodes and intermediate data is allocated in.
  Arena *arena() { return &arena_; }

  const BazelWorkspace &workspace() const { return workspace_; }

  // Register the "source_locator" for given given string-view range.
  // Range must be disjoint from all other ranges. Ownership of
  // "source_locator" is not taken over, ParsedProject just keeps track of
  // what ranges to delegate to for our own GetLocation() implementation.
  void RegisterLocationRange(std::string_view range,
                             const SourceLocator *source_locator);

  // -- SourceLocator implementation
  FileLocation GetLocation(std::string_view text) const final;
  std::string_view GetSurroundingLine(std::string_view text) const final;

 private:
  friend class ParsedProjectTestUtil;

  // like AddBuildFile(..package), but extract package from (workspace, path).
  // TODO: should not be needed, just an artifact of FillFromPattern() workings.
  ParsedBuildFile *AddBuildFile(Session &session,
                                const FilesystemPath &build_file,
                                std::string_view project);

  // Given package and content, parse. Main workhorse. Content is std::move()'d
  // thus by value.
  ParsedBuildFile *AddBuildFileContent(SessionStreams &message_out,
                                       const BazelPackage &package,
                                       std::string_view filename,
                                       std::string content);

  Arena arena_{1 << 20};
  const BazelWorkspace workspace_;
  int error_count_ = 0;
  Package2Parsed package_to_parsed_;
  DisjointRangeMap<std::string_view, const SourceLocator *> location_maps_;
};

// Convenience function to print a fully parsed project, recreated from the
// AST. Takes grep_regex into account for filtering.
void PrintProject(Session &session, const BazelPattern &pattern,
                  const ParsedProject &project);

}  // namespace bant
#endif  // BANT_PROJECT_PARDER_
