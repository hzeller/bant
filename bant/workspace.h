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

#ifndef BANT_WORKSPACE_
#define BANT_WORKSPACE_

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "bant/session.h"
#include "bant/util/file-utils.h"

namespace bant {
struct VersionedProject {
  static std::optional<VersionedProject> ParseFromDir(std::string_view);

  std::string project;
  std::string version;  // TODO: make this better to compare numerical versions

  auto operator<=>(const VersionedProject &other) const = default;
};

struct BazelWorkspace {
  // Returns the first Version that matches project name.
  std::optional<FilesystemPath> FindPathByProject(std::string_view name) const;

  // Project to directory.
  std::map<VersionedProject, FilesystemPath> project_location;
};

// Scan current directory for workspace files and create an index of all
// external projects the workspace references.
std::optional<BazelWorkspace> LoadWorkspace(Session &session);

// Some projects somewhat obfuscate the dependencies (looking at you, XLS), by
// putting things in various bzl files instead of a simple toplevel
// WORKSPACE or MODULE.bazel.
// Do some fallback by checking the directories these projects end up.
bool BestEffortAugmentFromExternalDir(BazelWorkspace &workspace);

}  // namespace bant

#endif  // BANT_WORKSPACE_
