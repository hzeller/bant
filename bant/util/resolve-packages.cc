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

#include "bant/util/resolve-packages.h"

#include <optional>
#include <set>

#include "absl/strings/str_cat.h"
#include "bant/frontend/project-parser.h"
#include "bant/types-bazel.h"
#include "bant/util/file-utils.h"
#include "bant/util/query-utils.h"

namespace bant {
namespace {

// TODO: this should use the BazelWorkspace to determine external project paths.
std::optional<FilesystemPath> PathForPackage(const BazelPackage &package) {
  if (package.project.empty()) {
    for (const std::string_view build_file : {"BUILD", "BUILD.bazel"}) {
      FilesystemPath test_path(package.path, build_file);
      if (test_path.can_read()) return test_path;
    }
    return std::nullopt;
  } else {
    for (const std::string_view glob_prefix : {"/", "~*/"}) {
      for (const std::string_view build_test : {"BUILD", "BUILD.bazel"}) {
        const auto found_build = Glob(absl::StrCat(
          BazelWorkspace::kExternalBaseDir, "/", package.project.substr(1),
          glob_prefix, package.path, "/", build_test));
        // TODO: just looking at first right now, should see if this is
        // a versioned one (need to look at MODULES.bazel to see what wanted)
        if (!found_build.empty()) {
          return found_build.front();
        }
      }
    }
    return std::nullopt;
  }
}

}  // namespace

// Looking what we have, record what other deps we need, find these and parse.
// Rinse/repeat until nothing more to add.
void ResolveMissingDependencies(ParsedProject *project,
                                const BazelPattern &pattern, bool verbose,
                                std::ostream &info_out, std::ostream &err_out) {
  std::vector<const ParsedBuildFile *> to_scan;
  std::set<BazelPackage> known_packages;

  // TODO: here, we base our starting point from the files we have, that
  // have been derived from the pattern.
  // It would probably be better to start out with and empty project and
  // handle all the pattern expansion of files to look at here.
  for (const auto &[_, parsed] : project->ParsedFiles()) {
    known_packages.insert(parsed->package);
    if (pattern.Match(parsed->package)) {
      to_scan.push_back(parsed.get());
    }
  }

  int rounds = 0;
  std::vector<BazelPackage> error_packages;
  std::vector<BazelPackage> work_list;
  while (!to_scan.empty()) {
    ++rounds;
    const size_t before_size = known_packages.size();
    for (const ParsedBuildFile *parsed : to_scan) {
      if (!parsed->ast) continue;
      const BazelPackage &current_package = parsed->package;
      query::FindTargets(
        parsed->ast, {"cc_library", "cc_test", "cc_binary"},
        [&](const query::Result &params) {
          // Look at all dependencies and add them if needed.
          std::vector<std::string_view> all_dependencies;
          query::ExtractStringList(params.deps_list, all_dependencies);

          for (std::string_view dep : all_dependencies) {
            auto target = BazelTarget::ParseFrom(dep, current_package);
            if (!target.has_value()) continue;
            const BazelPackage &maybe_need = target->package;
            if (known_packages.insert(maybe_need).second) {
              work_list.push_back(maybe_need);
            }
          }
        });
    }
    to_scan.clear();
    info_out << "\r" << before_size << " of " << known_packages.size()
             << " packages loaded";

    for (const BazelPackage &package : work_list) {
      auto path = PathForPackage(package);
      if (!path.has_value()) {
        error_packages.push_back(package);
        continue;
      }
      const auto parsed =
        project->AddBuildFile(*path, package, info_out, err_out);
      if (parsed) to_scan.push_back(parsed);
    }
    work_list.clear();
  }

  info_out << "\r" << project->ParsedFiles().size() << " of "
           << known_packages.size() << " packages loaded";
  if (!error_packages.empty()) {
    info_out << "; issues with " << error_packages.size();
  }
  if (verbose) {
    info_out << "; " << rounds << " rounds of following dependencies.";
  }
  info_out << "\n";

  if (verbose) {
    // TODO: maybe we should record where we have seen the package.
    for (const BazelPackage &missing : error_packages) {
      info_out << missing << ": Could not find BUILD file\n";
    }
  }
}
}  // namespace bant
