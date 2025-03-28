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

#ifndef BANT_WORKSPACE_
#define BANT_WORKSPACE_

#include <optional>
#include <string>
#include <string_view>

#include "bant/session.h"
#include "bant/types.h"
#include "bant/util/file-utils.h"

namespace bant {
struct VersionedProject {
  static std::optional<VersionedProject> ParseFromDir(std::string_view);
  enum Stratum {
    kRootProject,
    kWorkspaceDefined,
    kDirectoryFound,
    kUnknown,
  };

  std::string project;
  std::string version;  // TODO: make this better to compare numerical versions

  // Since we allow to also read just the filesystem structure to figure
  // out what external packages exist, remember the reliability of the
  // information. We might want to use that when a choice has to be made.
  Stratum stratum = Stratum::kWorkspaceDefined;

  auto operator<=>(const VersionedProject &other) const = default;
};

struct BazelWorkspace {
  using Map = OneToOne<VersionedProject, FilesystemPath>;

  // Returns the first Version that matches project name. Query can be with
  // or without leading '@".
  std::optional<FilesystemPath> FindPathByProject(std::string_view name) const;

  // Lower-level functionality returning the full map-entry. Same look-up
  // semantics.
  Map::const_iterator FindEntryByProject(std::string_view name) const;

  // Project to directory.
  Map project_location;
};

// Scan current directory for workspace files and create an index of all
// external projects the workspace references.
std::optional<BazelWorkspace> LoadWorkspace(Session &session);

// Some projects somewhat obfuscate the dependencies (looking at you, XLS),
// by putting deps in various bzl files instead of a simple toplevel
// WORKSPACE or MODULE.bazel.
// Do some fallback by checking the directories these projects end up.
// (Stored with lower stratum kDirectoryFound)
bool BestEffortAugmentFromExternalDir(Session &session,
                                      BazelWorkspace &workspace);

}  // namespace bant

#endif  // BANT_WORKSPACE_
