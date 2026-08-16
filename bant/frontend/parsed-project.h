// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
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

#ifndef BANT_PARSED_PROJECT_
#define BANT_PARSED_PROJECT_

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/macro-container.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/source-locator.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/types.h"
#include "bant/util/arena.h"
#include "bant/util/disjoint-range-map.h"
#include "bant/util/file-utils.h"
#include "bant/util/stat.h"
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
  // BUILD files
  using Package2Parsed =
    OneToOne<BazelPackage, std::unique_ptr<ParsedBuildFile>>;

  // Starlarkfiles.
  using Target2Parsed = OneToOne<BazelTarget, std::unique_ptr<ParsedBuildFile>>;

  // Variable to node mapping. Identifier string_view points to the
  // location it was assigned.
  using VariableBundle = absl::flat_hash_map<std::string_view, Node *>;

  ParsedProject(BazelWorkspace workspace, bool verbose,
                bool with_builtin_macros = true);

  // Given a BazelPattern, collect all the matching BUILD files and add to
  // project.
  // Returns number of build-files added.
  int FillFromPattern(Session &session, const BazelPatternBundle &bundle,
                      bool log_error_messages = true);

  // Given a package, load BUILD file and add to project.
  // Can return nullptr if the build file can not be loaded.
  ParsedBuildFile *GetOrAddPackage(Session &session,
                                   const BazelPackage &package,
                                   bool log_error_messages = true);

  // Given a starlark file, provide storage for variables and store their
  // content. If starlark file is not available yet, calls "variable_extractor"
  // to fill from the freshly generated AST.
  const VariableBundle &GetOrAddStarlarkContent(
    Session &session, std::string_view starlark_ref,
    const BazelTarget &starlark,
    const std::function<void(List *ast, VariableBundle *)> &variable_extractor);

  // Iterate through project in a thread-safe way. No addition of new packages
  // file is allowed while iterating.
  void ForEach(const std::function<void(const BazelPackage &,
                                        ParsedBuildFile &)> &cb) const {
    const absl::MutexLock scoped_lock(package_to_parsed_mutex_);
    for (const auto &[package, file] : package_to_parsed_) {
      cb(package, *file);
    }
  }

  // Look up parse file given the package, or nullptr, if not parsed (yet).
  // Caller is responsible to not modify returned object in multiple threads.
  const ParsedBuildFile *FindParsedOrNull(const BazelPackage &package) const;

  // Some stats.
  int error_count() const { return error_count_; }

  // Arena all Nodes and intermediate data is allocated in.
  Arena *arena() { return &arena_; }

  // Number of packages loaded.
  size_t size() const;

  const BazelWorkspace &workspace() const { return workspace_; }

  // TODO: the folloing just delegate to macro container, but should be
  // refactored away
  Node *FindMacro(std::string_view name, const BazelPackage &package) const {
    return macros_.FindMacro(name, package);
  }

  absl::Status SetBuiltinMacroContent(std::string_view content) {
    return macros_.SetBuiltinMacroContent(content);
  }

  // Register the "source_locator" for given given string-view range.
  // Range must be disjoint from all other ranges. Ownership of
  // "source_locator" is not taken over, ParsedProject just keeps track of
  // what ranges to delegate to for our own GetLocation() implementation.
  void RegisterLocationRange(std::string_view range,
                             const SourceLocator *source_locator);

  // -- SourceLocator implementation
  FileLocation GetLocation(std::string_view text) const final;
  std::string_view GetSurroundingLine(std::string_view text) const final;

  // TODO: mapping string view to location back to bazel package.
  // e.g. mapping back filegroup string-views to package.
  // BazelPackage GetPackageFor(std::string_view text) const;

 private:
  friend class ParsedProjectTestUtil;

  // Given package and content, parse. Main workhorse.
  // Content is std::move()'d thus by value.
  // "read_stat" contains information how long it took to aquire content and
  // is added to the corresponding stats.
  // Will always return a ParsedBuildFile
 public:  // Should not be public, but used in debug output in cli-commands.cc
  ParsedBuildFile *AddBuildFileContent(
    Session &session, const BazelPackage &package, const FilesystemPath &file,
    std::string content, const Stat &read_stat, bool log_error_messages = true);

 private:
  // like AddBuildFile(..package), but extract package from (workspace, path).
  // TODO: should not be needed, just an artifact of FillFromPattern() workings.
  ParsedBuildFile *AddBuildFile(Session &session,
                                const FilesystemPath &build_file,
                                std::string_view project,
                                bool log_error_messages);
  // Parse build file for given package reading from filename.
  ParsedBuildFile *AddBuildFile(Session &session,
                                const FilesystemPath &build_file,
                                const BazelPackage &package,
                                bool log_error_messages);

  void LoadBantMacrosInPackage(Session &session, const BazelPackage &package);

  Arena arena_{1 << 20};
  const BazelWorkspace workspace_;
  int error_count_ = 0;

  mutable absl::Mutex package_to_parsed_mutex_;
  Package2Parsed package_to_parsed_ ABSL_GUARDED_BY(package_to_parsed_mutex_);

  mutable absl::Mutex location_map_lock_;
  DisjointRangeMap<std::string_view, const SourceLocator *> location_maps_
    ABSL_GUARDED_BY(location_map_lock_);

  MacroContainer macros_;

  Target2Parsed starlark_to_parsed_;  // Starlark files.
  OneToOne<BazelTarget, std::unique_ptr<VariableBundle>> starlark_variables_;
};

}  // namespace bant
#endif  // BANT_PARSED_PROJECT_
