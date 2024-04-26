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

#include "bant/workspace.h"

#include <sstream>

#include "absl/strings/str_cat.h"
#include "bant/frontend/parser.h"
#include "bant/frontend/scanner.h"
#include "bant/util/arena.h"
#include "bant/util/query-utils.h"

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
    result.project = dir;
  }
  return result;
}

std::optional<FilesystemPath> BazelWorkspace::FindPathByProject(
  std::string_view name) const {
  if (name.empty()) return std::nullopt;
  if (name[0] == '@') name.remove_prefix(1);
  VersionedProject query{.project = std::string(name), .version = ""};
  auto found = project_location.lower_bound(query);
  if (found == project_location.end()) return std::nullopt;
  if (found->first.project != name) return std::nullopt;
  return found->second;
}

bool BestEffortAugmentFromExternalDir(BazelWorkspace &workspace) {
  bool any_found = false;
  const std::string pattern = absl::StrCat(kExternalBaseDir, "/*");
  for (const FilesystemPath &project_dir : Glob(pattern)) {
    if (!project_dir.is_directory()) continue;
    std::string_view project_name = project_dir.filename();
    auto project_or = VersionedProject::ParseFromDir(project_name);
    if (!project_or) continue;
    // If there is any version of that project already, don't bother.
    if (!workspace.FindPathByProject(project_or->project)) {
      any_found = true;
      workspace.project_location[*project_or] = project_dir;
    }
  }
  return any_found;
}

std::optional<BazelWorkspace> LoadWorkspace(Session &session) {
  bool workspace_found = false;
  BazelWorkspace workspace;
  bool did_bazel_run_already_printed = false;
  for (const auto ws :
       {"WORKSPACE", "WORKSPACE.bazel", "WORKSPACE.bzlmod", "MODULE.bazel"}) {
    std::optional<std::string> content = ReadFileToString(FilesystemPath(ws));
    if (!content.has_value()) continue;
    // TODO: maybe store the names_content for later use. Right now we only
    // parse once, then don't worry about keeping content.
    NamedLineIndexedContent named_content(ws, content.value());
    Arena arena(1 << 16);

    Scanner scanner(named_content);
    std::stringstream error_collect;
    Parser parser(&scanner, &arena, session.info());
    Node *ast = parser.parse();
    if (ast) workspace_found = true;
    query::FindTargets(
      ast, {"http_archive", "bazel_dep"}, [&](const query::Result &result) {
        // Sometimes, the versin is attached to the dirs, somtimes not. Not
        // clear why, but check for both if we have a version.
        std::vector<std::string> search_dirs;
        if (!result.version.empty()) {
          search_dirs.push_back(absl::StrCat(result.name, "~", result.version));
        }
        search_dirs.emplace_back(result.name);

        // Also a plausible location when archive_override() is used:
        search_dirs.push_back(absl::StrCat(result.name, "~override"));

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
            // Should we extract version from path ?
          }
        }

        if (!project_dir_found) {
          named_content.Loc(session.info(), result.name)
            << " Can't find extracted project '" << result.name << "'\n";
          if (!did_bazel_run_already_printed) {
            session.info() << "Note: need to run a bazel build at least once "
                              "to extract external projects\n";
            did_bazel_run_already_printed = true;
          }
          return;
        }

        VersionedProject project;
        project.project =
          result.repo_name.empty() ? result.name : result.repo_name;
        project.version = result.version;
        workspace.project_location[project] = path;
      });
  }
  if (!workspace_found) return std::nullopt;
  return workspace;
}
}  // namespace bant
