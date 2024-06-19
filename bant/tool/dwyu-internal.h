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

#ifndef BANT_TOOL_DWYU_INTERNAL_
#define BANT_TOOL_DWYU_INTERNAL_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "bant/explore/header-providers.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/tool/edit-callback.h"
#include "bant/types-bazel.h"
#include "bant/types.h"

namespace bant {
// The DWYUGenerator is the underlying implementation, for which
// CreateDependencyEdits() is the façade. Typically not used directly,
// just needed in tests.
class DWYUGenerator {
 public:
  DWYUGenerator(Session &session, const ParsedProject &project,
                EditCallback emit_deps_edit);
  virtual ~DWYUGenerator() = default;

  // Return number of targets that matched pattern and have been processed.
  size_t CreateEditsForPattern(const BazelPattern &pattern);

 protected:
  // Extracted source file.
  struct SourceFile {
    std::string content;  // Content of the file
    std::string path;     // Path relative to current directory.
    bool is_generated;    // This is the output of some other rule.
  };

  // Try to find the given file in the soruce tree or the generated tree,
  // and return content and path. Virtual, to make this class testable.
  virtual std::optional<SourceFile> TryOpenFile(std::string_view source_file);

 private:
  // Extract all the known targets in project and remember corresponding node
  // in case later inspection is needed (e.g. for visibility).
  void InitKnownLibraries();

  // Given a bunch of sources, grep their content (using TryOpenFile() to
  // get it), and look up all targets providing them.
  // For some, there can be alternatives, so this is a vector of sets.
  // Report in "all_headers_accounted_for", that we found
  // a library for each of the headers we have seen.
  // This is important as only then we can confidently suggest removals in that
  // target.
  std::vector<absl::btree_set<BazelTarget>> DependenciesNeededBySources(
    const BazelTarget &target, const ParsedBuildFile &build_file,
    const std::vector<std::string_view> &sources,
    bool *all_headers_accounted_for);

  void CreateEditsForTarget(const BazelTarget &target,
                            const query::Result &details,
                            const ParsedBuildFile &build_file);

  bool IsAlwayslink(const BazelTarget &target) const;
  bool CanSee(const BazelTarget &target, const BazelTarget &dep) const;

  Session &session_;
  const ParsedProject &project_;
  const EditCallback emit_deps_edit_;
  OneToN<BazelTarget, BazelTarget> aliased_by_;
  ProvidedFromTargetSet headers_from_libs_;
  ProvidedFromTarget files_from_genrules_;
  absl::btree_map<BazelTarget, query::Result> known_libs_;
};
}  // namespace bant

#endif  // BANT_TOOL_DWYU_INTERNAL_
