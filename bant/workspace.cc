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

#include "bant/workspace.h"

#include <array>
#include <cstddef>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/parser.h"
#include "bant/frontend/scanner.h"
#include "bant/session.h"
#include "bant/util/arena.h"
#include "bant/util/file-utils.h"

namespace bant {

static constexpr std::string_view kExternalBaseDir =
  "bazel-out/../../../external";

/*static*/ std::optional<VersionedProject> VersionedProject::ParseFromDir(
  std::string_view dir) {
  if (dir.empty()) return std::nullopt;

  VersionedProject result;
  auto tilde_pos = dir.find_first_of('~');
  if (tilde_pos != std::string_view::npos) {
    if (tilde_pos == 0) return std::nullopt;
    result.project = dir.substr(0, tilde_pos);
    result.version = dir.substr(tilde_pos + 1);
  } else {
    if (dir.ends_with("+")) dir.remove_suffix(1);  // bazel8-ism
    result.project = dir;
  }
  return result;
}

BazelWorkspace::Map::const_iterator BazelWorkspace::FindEntryByProject(
  std::string_view name) const {
  if (name.empty()) return project_location.end();
  if (name[0] == '@') name.remove_prefix(1);
  const VersionedProject query{.project = std::string(name), .version = ""};
  auto found = project_location.lower_bound(query);
  if (found == project_location.end()) return found;
  if (found->first.project != name) return project_location.end();
  return found;
}

std::optional<FilesystemPath> BazelWorkspace::FindPathByProject(
  std::string_view name) const {
  auto found = FindEntryByProject(name);
  if (found == project_location.end()) return std::nullopt;
  return found->second;
}

bool BestEffortAugmentFromExternalDir(BazelWorkspace &workspace) {
  bool any_found = false;
  const std::string pattern = absl::StrCat(kExternalBaseDir, "/*");
  for (const FilesystemPath &project_dir : Glob(pattern)) {
    if (!project_dir.is_directory()) continue;
    const std::string_view project_name = project_dir.filename();
    auto project_or = VersionedProject::ParseFromDir(project_name);
    if (!project_or) continue;
    // If there is any version of that project already, don't bother.
    if (!workspace.FindPathByProject(project_or->project)) {
      any_found = true;
      project_or->stratum = VersionedProject::Stratum::kDirectoryFound;
      workspace.project_location[*project_or] = project_dir;
    }
  }
  return any_found;
}

static bool LoadWorkspaceFromFile(Session &session,
                                  const FilesystemPath &ws_file,
                                  std::ostream &msg_stream,
                                  BazelWorkspace *workspace) {
  const std::optional<std::string> content = ReadFileToString(ws_file);
  if (!content.has_value()) return false;

  // TODO: maybe store the names_content for later use to be able to point
  // to specific places something is declared.
  NamedLineIndexedContent named_content(ws_file.path(), content.value());
  Arena arena(1 << 16);
  Scanner scanner(named_content);
  Parser parser(&scanner, &arena, session.info());
  Node *ast = parser.parse();
  if (!ast) return false;

  query::FindTargets(
    ast, {"http_archive", "bazel_dep"}, [&](const query::Result &result) {
      // Sometimes, the version is attached to the dirs (bazel 6), somtimes
      // not (before bazel 6: plain file, at bazel 7: just ~, at bazel 8 '+')
      // Check for both if we have a version.
      std::vector<std::string> search_dirs;
      if (!result.version.empty()) {
        search_dirs.push_back(absl::StrCat(result.name, "~", result.version));
      }
      search_dirs.emplace_back(result.name);

      // Also a plausible location when archive_override() is used:
      search_dirs.push_back(absl::StrCat(result.name, "~override"));
      search_dirs.push_back(absl::StrCat(result.name, "+"));  // bazel8-ism

      FilesystemPath path;
      bool project_dir_found = false;
      for (const std::string_view dir : search_dirs) {
        path = FilesystemPath(kExternalBaseDir, dir);
        if (!path.is_directory() || !path.can_read()) continue;
        project_dir_found = true;
        break;
      }

      if (!project_dir_found) {
        // Maybe we got a different version ?
        auto maybe_match =
          Glob(absl::StrCat(kExternalBaseDir, "/", result.name, "~*"));
        if (!maybe_match.empty()) {
          path = maybe_match.front();
          project_dir_found = path.is_directory() && path.can_read();
        }
      }

      if (!project_dir_found) {
        named_content.Loc(msg_stream, result.name)
          << " Can't find extracted project '" << result.name << "'\n";
        return;
      }

      VersionedProject project;
      project.project =
        result.repo_name.empty() ? result.name : result.repo_name;
      project.version = result.version;
      workspace->project_location[project] = path;
      // TODO: if this is a repo_name alias, would we ever need the original
      // name stored with a different (less authoritative) stratum ?
    });

  return true;
}

std::optional<BazelWorkspace> LoadWorkspace(Session &session) {
  bool workspace_found = false;
  BazelWorkspace workspace;

  constexpr std::array<std::string_view, 4> ws_files = {
    "WORKSPACE", "WORKSPACE.bazel",  //
    "WORKSPACE.bzlmod", "MODULE.bazel"};

  // We collect messages for old and new style workspaces separately.
  std::stringstream old_workspace_msg;
  std::stringstream new_workspace_msg;

  for (size_t i = 0; i < ws_files.size(); ++i) {
    const std::string_view ws = ws_files[i];
    std::ostream &msg_stream = i < 2 ? old_workspace_msg : new_workspace_msg;

    workspace_found |= LoadWorkspaceFromFile(session, FilesystemPath(ws),
                                             msg_stream, &workspace);
  }

  // Only if there are issues in old _and_ new workspace set-up, it indicates
  // that workspace have not be expanded yet by bazel.
  if (!old_workspace_msg.str().empty() && !new_workspace_msg.str().empty()) {
    // Old and new workspace both had issues.
    session.info() << old_workspace_msg.str();
    session.info() << new_workspace_msg.str();
    session.info() << "Note: need to run a bazel build at least once "
                      "to extract external projects\n";
  }
  if (!workspace_found) return std::nullopt;
  return workspace;
}
}  // namespace bant
