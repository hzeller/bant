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

// TODO: While this follows all the dependencies, it still requries some
// hacks around protocol buffers (as we don't know the *.bzl definitions).

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
#include "bant/explore/dependency-graph.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/types-bazel.h"
#include "bant/util/file-utils.h"
#include "bant/workspace.h"
#include "re2/re2.h"

namespace bant {

// Make quoted strings a little less painful to print to C++ streams
struct q {
  std::string_view value;
};
std::ostream &operator<<(std::ostream &out, const q &quoted_str) {
  out << "\"" << quoted_str.value << "\"";
  return out;
}

// Common typical options considered for the compiler.
static constexpr std::string_view kCommonDefaultOptions[] = {
  "-xc++",
  "-U_FORTIFY_SOURCE",
  "-O2",
  "-DNDEBUG",
};

// Return options found in the bazelrc in the sequence they arrive.
// TODO: just emit the last winning option if multiple same options found.
//       (right now it emits the _first_)
// TODO: allow for configuration specific to operating sytems, but not special
//       configs e.g. build:asan
// TODO: needs test :)
std::vector<std::string> ExtractOptionsFromBazelrc(std::string_view content) {
  // Hack: for cxx options that start with dash (to avoid picking up options
  // meant for Windows that start with slash (we don't do system-specific
  // evaluation)
  // Need to be a space after build|test so that we don't capture special
  // configurations such as build:asan.
  static const LazyRE2 kCxxExtract{
    R"/(--(?:host_)?cxxopt\s*=?\s*['"]?(-[^\s"']+))/"};
  std::vector<std::string> result;
  const auto bazelrc = ReadFileToString(FilesystemPath(".bazelrc"));
  if (!bazelrc.has_value()) return result;

  std::string_view run(*bazelrc);
  std::string_view cxx_opt;
  absl::flat_hash_set<std::string_view> already_seen;

  while (RE2::FindAndConsume(&run, *kCxxExtract, &cxx_opt)) {
    if (already_seen.insert(cxx_opt).second) {
      result.emplace_back(cxx_opt);
    }
  }

  // Hack: when this is defined, this implies -DGTEST_HAS_ABSL
  static const LazyRE2 kAbslGtest{"define.*absl=1"};
  if (RE2::PartialMatch(*bazelrc, *kAbslGtest)) {
    result.emplace_back("-DGTEST_HAS_ABSL=1");
  }

  return result;
}

static std::vector<std::string> ExtractOptionsFromBazelrcFile() {
  std::vector<std::string> result;
  const auto bazelrc = ReadFileToString(FilesystemPath(".bazelrc"));
  if (!bazelrc.has_value()) return result;
  return ExtractOptionsFromBazelrc(*bazelrc);
}

// Hack to accomodate protocol buffers.
// They depend on some virtual includes that we can't directly see from
// the targets. Add that in manually.
////
// This should be done differntly by mirroring what a cc_proto_library()
// actually expands to as cc_library with their corresponding deps = []
// (without having to parse the convoluted *.bzl file).
// Broken out in separate function to easily remove this hack later.
static void ProtobufHack(const BazelTarget &target,
                         const BazelWorkspace &workspace, bool is_proto_library,
                         absl::flat_hash_set<std::string> *already_seen,
                         std::vector<std::string> &result) {
  const std::string_view protobuf_project = target.package.project;
  if (!protobuf_project.contains("protobuf")) return;  // not interesting.
  auto protobuf_dir = workspace.FindPathByProject(protobuf_project);
  if (!protobuf_dir.has_value()) return;

  // First time we see a protobuf dependecy, add the usual suspect of
  // virtual includes
  if (already_seen->insert("protobuf-extra-include-hack").second) {
    constexpr struct PackageTarget {
      const char *package;
      const char *target;
    } kProtoTargets[] = {
      {"", "protobuf_headers"},
      {"", "protobuf"},
      {"", "protobuf_nowkt"},
      {"", "port"},
      {"", "arena"},
      {"", "arena_align"},
      {"", "arena_allocation_policy"},
      {"", "arena_cleanup"},
      {"", "protobuf_lite"},
      {"", "internal_visibility"},
      {"", "string_block"},
      {"stubs/", "lite"},
      {"io/", "io"},
    };
    for (const PackageTarget extra_inc : kProtoTargets) {
      const std::string virt_incdir =
        absl::StrCat("bazel-bin/external/", protobuf_dir->filename(),
                     "/src/google/protobuf/", extra_inc.package,
                     "_virtual_includes/", extra_inc.target);
      if (already_seen->insert(virt_incdir).second) {
        result.emplace_back(virt_incdir);
      }
    }
  }

  // Extra hack: if we depend on some of the common any_proto, timestamp_proto
  // proto buffers, add the headers here.
  if (is_proto_library) {
    const std::string virt_incdir = absl::StrCat(
      "bazel-bin/external/", protobuf_dir->filename(),
      "/src/google/protobuf/_virtual_includes/", target.target_name);
    if (already_seen->insert(virt_incdir).second) {
      result.emplace_back(virt_incdir);
    }
  }
}

// The grpc_cc_library() adds an implicit "include/", but since we can't see
// the corresponding *.bzl file, apply this hack here.
static void GRPCHack(const BazelTarget &target, const BazelWorkspace &workspace,
                     absl::flat_hash_set<std::string> *already_seen,
                     std::vector<std::string> &result) {
  const std::string_view external_project = target.package.project;
  auto ext_dir = workspace.FindPathByProject(external_project);
  if (!ext_dir) return;
  const std::string prefix_applied = absl::StrCat(ext_dir->path(), "/include");
  if (already_seen->insert(prefix_applied).second) {
    result.emplace_back(prefix_applied);
  }
}

static std::vector<std::string> CollectIncDirs(
  Session &session, const BazelTargetMatcher &pattern, ParsedProject *project) {
  std::vector<std::string> result;

  result.emplace_back(".");                   // Our sources.
  result.emplace_back("bazel-bin");           // Generated files.
  result.emplace_back("bazel-out/../../..");  // Root for all external/

  // All the -I (or more precisely: -iquote) directories.
  const BazelWorkspace &workspace = project->workspace();
  absl::flat_hash_set<std::string> already_seen;
  BuildDependencyGraph(
    session, pattern, 30, project,
    [&](const BazelTarget &target, const query::Result &details) {
      const BazelPackage &current_package = target.package;
      // If we're one of those targets that come with the own -I prefix,
      // add all these.
      const auto inc_dirs = query::ExtractStringList(details.includes_list);
      for (const std::string_view inc_dir : inc_dirs) {
        const std::string inc_path =
          current_package.FullyQualifiedFile(workspace, inc_dir);
        if (!already_seen.insert(inc_path).second) {
          continue;
        }
        result.emplace_back(inc_path);
      }

      // bazel generates virtual include dirs when "include_prefix" is set.
      if (!details.include_prefix.empty()) {
        // TODO: this might be different for external and not. Right now
        // we're focused on external projects, such as protobuf that seems
        // to use this feature.
        const std::string_view external_project = target.package.project;
        const std::string_view target_path = target.package.path;
        auto ext_dir = workspace.FindPathByProject(external_project);
        if (ext_dir.has_value()) {
          const std::string virt_incdir = absl::StrCat(
            "bazel-bin/external/", ext_dir->filename(), "/", target_path,
            "/_virtual_includes/", target.target_name);
          if (already_seen.insert(virt_incdir).second) {
            result.emplace_back(virt_incdir);
          }
        }
      }

      if (!details.strip_include_prefix.empty()) {
        const std::string_view external_project = target.package.project;
        auto ext_dir = workspace.FindPathByProject(external_project);
        if (ext_dir) {
          const std::string prefix_applied =
            absl::StrCat(ext_dir->path(), "/", details.strip_include_prefix);
          if (already_seen.insert(prefix_applied).second) {
            result.emplace_back(prefix_applied);
          }
        }
      }

      // If we depend on anything that looks like protobuf, apply this hack.
      const bool is_proto_library = details.rule == "proto_library";
      ProtobufHack(target, workspace, is_proto_library, &already_seen, result);

      // GRPC requires a hack.
      if (details.rule == "grpc_cc_library") {
        GRPCHack(target, workspace, &already_seen, result);
      }

      // Now, let's check out the dependencies and see that all of the
      // referenced external projects are covered.
      const auto deps = query::ExtractStringList(details.deps_list);
      for (const std::string_view dependency_target : deps) {
        auto requested_dep =
          BazelTarget::ParseFrom(dependency_target, current_package);
        if (!requested_dep.has_value()) {
          continue;
        }

        const std::string &external_project = requested_dep->package.project;
        if (external_project.empty()) {
          continue;  // Include path of our project is implicit
        }
        if (!already_seen.insert(external_project).second) {
          continue;
        }
        auto ext_dir = workspace.FindPathByProject(external_project);
        if (!ext_dir.has_value()) continue;  // ¯\_(ツ)_/¯

        // Direct path provided into the sources.
        result.emplace_back(ext_dir->path());

        // Generated files
        const std::string_view just_project_dir = ext_dir->filename();
        const std::string gen_inc =
          absl::StrCat("bazel-bin/external/", just_project_dir);
        result.emplace_back(gen_inc);
      }
    });

  return result;
}

static std::string EncodeFlagsIncludeAsJson(Session &session,
                                            const BazelTargetMatcher &pattern,
                                            ParsedProject *project) {
  constexpr std::string_view kIndent = "      ";

  std::stringstream out;

  // All the cxx options mentioned in the .bazelrc
  for (const std::string &cxxopt : ExtractOptionsFromBazelrcFile()) {
    out << kIndent << q{cxxopt} << ",\n";
  }

  for (const std::string &inc : CollectIncDirs(session, pattern, project)) {
    out << kIndent << q{"-iquote"} << ", " << q{inc} << ",\n";
  }

  return out.str();
}

static void WriteCompilationDBEntry(const ParsedProject &project,
                                    const BazelPackage &package,
                                    const query::Result &details,
                                    const std::string &cwd,
                                    const std::string &external_inc_json,
                                    std::set<std::string> *already_written,
                                    std::ostream &out) {
  std::vector<std::string_view> sources;
  query::AppendStringList(details.srcs_list, sources);
  query::AppendStringList(details.hdrs_list, sources);

  for (const auto src : sources) {
    const std::string abs_src =
      package.FullyQualifiedFile(project.workspace(), src);
    if (!already_written->insert(abs_src).second) continue;
    out << "  {\n";
    out << "    " << q{"file"} << ": " << q{abs_src} << ",\n";
    out << "    " << q{"arguments"} << ": [\n";
    out << "      " << q{"gcc"} << ",\n";
    for (const std::string_view option : kCommonDefaultOptions) {
      out << "      " << q{option} << ",\n";
    }
    out << external_inc_json;
    out << "      " << q{"-c"} << ", " << q{abs_src} << ",\n";
    out << "     ],\n";
    out << "     " << q{"directory"} << ": " << q{cwd} << "\n";
    out << "  },\n";
  }
}

static void WriteCompilationDB(Session &session,
                               const BazelTargetMatcher &pattern,
                               ParsedProject *project) {
  std::ostream &out = session.out();
  const std::string cwd = std::filesystem::current_path().string();

  // Instead of being specific which *.cc file uses which external
  // headers (which would require to recusively follow all its dependencies),
  // let's just extract all external projects ever used and prepare them
  // as one include blob.
  // More robust currently, but should probably be more specific per file,
  // once we know what we're doing :)
  const std::string external_inc_json =
    EncodeFlagsIncludeAsJson(session, pattern, project);

  std::set<std::string> already_written;
  out << "[\n";
  for (const auto &[_, parsed_package] : project->ParsedFiles()) {
    const BazelPackage &current_package = parsed_package->package;
    if (!pattern.Match(current_package)) {
      continue;
    }

    query::FindTargets(
      parsed_package->ast, {"cc_library", "cc_binary", "cc_test"},
      [&](const query::Result &details) {
        auto target = current_package.QualifiedTarget(details.name);
        if (!target.has_value() || !pattern.Match(*target)) {
          return;
        }
        WriteCompilationDBEntry(*project, current_package, details,  //
                                cwd, external_inc_json, &already_written, out);
      });
  }
  out << "]\n";
}

static void WriteCompilationFlags(Session &session,
                                  const BazelTargetMatcher &pattern,
                                  ParsedProject *project) {
  // All the cxx options mentioned in the .bazelrc
  for (const std::string &cxxopt : ExtractOptionsFromBazelrcFile()) {
    session.out() << cxxopt << "\n";
  }

  for (const std::string &inc : CollectIncDirs(session, pattern, project)) {
    session.out() << "-I" << inc << "\n";
  }
}

// Public interface
void WriteCompilationFlags(Session &session, const BazelTargetMatcher &pattern,
                           ParsedProject *project, bool as_compilation_db) {
  if (as_compilation_db) {
    WriteCompilationDB(session, pattern, project);
  } else {
    WriteCompilationFlags(session, pattern, project);
  }
}

}  // namespace bant
