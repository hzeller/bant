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

#include "bant/tool/compilation-db.h"

#include <filesystem>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/util/file-utils.h"
#include "bant/workspace.h"
#include "re2/re2.h"

namespace bant {

// Make quoted strings a little less painful to read and write w/ C++ streams
struct q {
  std::string_view value;
};
std::ostream &operator<<(std::ostream &out, const q &quoted_str) {
  out << "\"" << quoted_str.value << "\"";
  return out;
}

static std::set<std::string> ExtractCxxOptionsFromBazelrc() {
  // Hack: for cxx options that start with dash (to avoid picking up options
  // meant for Windows that start with slash (we don't do system-specific
  // evaluation)
  static const LazyRE2 kCxxExtract{R"/(--(?:host_)?cxxopt\s*=?\s*(-[^\s]+))/"};

  std::set<std::string> result;
  const auto bazelrc = ReadFileToString(FilesystemPath(".bazelrc"));
  if (!bazelrc.has_value()) return result;

  std::string_view run(*bazelrc);
  std::string_view cxx_opt;

  while (RE2::FindAndConsume(&run, *kCxxExtract, &cxx_opt)) {
    result.insert(std::string{cxx_opt});
  }
  return result;
}

static void WriteCompilationDBEntry(const ParsedProject &project,
                                    const BazelPackage &package,
                                    const query::Result &details,
                                    const std::string &cwd,
                                    const std::string &external_inc_json,
                                    std::ostream &out) {
  std::vector<std::string_view> sources;
  query::AppendStringList(details.srcs_list, sources);
  query::AppendStringList(details.hdrs_list, sources);

  for (const auto src : sources) {
    const std::string abs_src = package.QualifiedFile(src);
    out << "  {\n";
    out << "    " << q{"file"} << ": " << q{abs_src} << ",\n";
    out << "    " << q{"arguments"} << ": [\n";
    out << "      " << q{"gcc"} << ", " << q{"-xc++"} << ", " << q{"-Wall"}
        << ",\n";
    out << "      " << q{"-iquote"} << ", " << q{"."} << ",\n";
    out << "      " << q{"-iquote"} << ", " << q{"bazel-bin"} << ",\n";
    out << external_inc_json;
    out << "      " << q{"-c"} << ", " << q{abs_src} << ",\n";
    out << "     ],\n";
    out << "     " << q{"directory"} << ": " << q{cwd} << "\n";
    out << "  },\n";
  }
}

static std::string CollectGlobalFlagsAndIncDirs(const ParsedProject &project) {
  constexpr std::string_view kIndent = "      ";

  std::stringstream out;

  // All the cxx options mentioned in the .bazelrc
  for (const std::string &cxxopt : ExtractCxxOptionsFromBazelrc()) {
    out << kIndent << q{cxxopt} << ",\n";
  }

  // All the -I (or more precisely: -iquote) directories.
  const BazelWorkspace &workspace = project.workspace();
  absl::flat_hash_set<std::string> already_seen;
  for (const auto &[_, parsed_package] : project.ParsedFiles()) {
    const BazelPackage &current_package = parsed_package->package;
    query::FindTargets(
      parsed_package->ast, {"cc_library", "cc_binary", "cc_test"},
      [&](const query::Result &details) {
        auto target = BazelTarget::ParseFrom(absl::StrCat(":", details.name),
                                             current_package);
        // If we're one of those targets that come with the own -I prefix,
        // add all these.
        const auto inc_dirs = query::ExtractStringList(details.includes_list);
        for (const std::string_view inc_dir : inc_dirs) {
          const std::string inc_path =
            current_package.QualifiedFile(workspace, inc_dir);
          if (!already_seen.insert(inc_path).second) continue;
          out << kIndent << q{"-iquote"} << ", " << q{inc_path} << ",\n";
        }

        // Now, let's check out the dependencies and see that all of these
        // are covered.
        const auto deps = query::ExtractStringList(details.deps_list);
        for (const std::string_view dependency_target : deps) {
          auto requested_dep =
            BazelTarget::ParseFrom(dependency_target, current_package);
          if (!requested_dep.has_value()) {
            continue;
          }

          // Other than that, add all external projects to the incdirs.
          const std::string &external_project = requested_dep->package.project;
          if (external_project.empty()) {
            continue;  // Include path of our project is implicit
          }
          if (!already_seen.insert(external_project).second) {
            continue;
          }
          auto ext_path = workspace.FindPathByProject(external_project);
          if (!ext_path.has_value()) continue;
          out << kIndent << q{"-iquote"} << ", " << q{ext_path->path()}
              << ",\n";
        }
      });
  }

  return out.str();
}

void WriteCompilationDB(Session &session, const ParsedProject &project,
                        const BazelPattern &pattern) {
  std::ostream &out = session.out();
  const std::string cwd = std::filesystem::current_path().string();

  // Instead of being specific which *.cc file uses which external
  // headers (which would require to recusively follow all the dependencies),
  // let's just extract all external projects ever used and prepare them
  // as one include blob. More robust.
  const std::string external_inc_json = CollectGlobalFlagsAndIncDirs(project);

  out << "[\n";
  for (const auto &[_, parsed_package] : project.ParsedFiles()) {
    const BazelPackage &current_package = parsed_package->package;
    if (!pattern.Match(current_package)) {
      continue;
    }

    query::FindTargets(
      parsed_package->ast, {"cc_library", "cc_binary", "cc_test"},
      [&](const query::Result &details) {
        auto target = BazelTarget::ParseFrom(absl::StrCat(":", details.name),
                                             current_package);
        if (!target.has_value() || !pattern.Match(*target)) {
          return;
        }
        WriteCompilationDBEntry(project, current_package, details,  //
                                cwd, external_inc_json, out);
      });
  }
  out << "]\n";
}
}  // namespace bant
