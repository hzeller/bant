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

// TODO: this is fairly coarse-grained, as it emits all include directories
// seen in all external projects for all files.

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

static std::set<std::string> ExtractCxxOptionsFromBazelrc() {
  // Hack: for cxx options that start with dash (to avoid picking up options
  // meant for Windows that start with slash (we don't do system-specific
  // evaluation)
  static const LazyRE2 kCxxExtract{
    R"/(--(?:host_)?cxxopt\s*=?\s*['"]?(-[^\s"']+))/"};

  std::set<std::string> result;
  const auto bazelrc = ReadFileToString(FilesystemPath(".bazelrc"));
  if (!bazelrc.has_value()) return result;

  std::string_view run(*bazelrc);
  std::string_view cxx_opt;

  while (RE2::FindAndConsume(&run, *kCxxExtract, &cxx_opt)) {
    result.insert(std::string{cxx_opt});
  }

  // Hack: when this is defined, this implies -DGTEST_HAS_ABSL
  static const LazyRE2 kAbslGtest{"define.*absl=1"};
  if (RE2::PartialMatch(*bazelrc, *kAbslGtest)) {
    result.insert("-DGTEST_HAS_ABSL=1");
  }

  return result;
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

// Hack to accomodate protocol buffers.
// They depend on some virtual includes that we can't directly see from
// the targets. Add that in manually.
////
// This should be done differntly by mirroring what a cc_proto_library()
// actually expands to as cc_library with their corresponding deps = []
// (without having to parse the convoluted *.bzl file).
// Broken out in separate function to easily remove this hack later.
static void ProtobufAnyPbHack(const BazelTarget &target,
                              absl::flat_hash_set<std::string> *already_seen,
                              std::string_view indent, std::stringstream &out) {
  const std::string_view external_project = target.package.project;
  if (!external_project.contains("protobuf")) return;  // not interesting.
  if (!already_seen->insert("protobuf-extra-include-hack").second) return;
  constexpr struct PackageTarget {
    const char *package;
    const char *target;
  } kProtoTargets[] = {
    {"", "protobuf_headers"},
    {"stubs/", "lite"},
    {"io/", "io"},
  };
  for (const PackageTarget extra_inc : kProtoTargets) {
    const std::string virt_incdir =
      absl::StrCat("bazel-bin/external/", external_project.substr(1),
                   "/src/google/protobuf/", extra_inc.package,
                   "_virtual_includes/", extra_inc.target);
    if (already_seen->insert(virt_incdir).second) {
      out << indent << q{"-iquote"} << ", " << q{virt_incdir} << ",\n";
    }
  }
}

static std::string CollectGlobalFlagsAndIncDirs(const ParsedProject &project) {
  constexpr std::string_view kIndent = "      ";

  std::stringstream out;

  // All the cxx options mentioned in the .bazelrc
  for (const std::string &cxxopt : ExtractCxxOptionsFromBazelrc()) {
    out << kIndent << q{cxxopt} << ",\n";
  }

  // Headers for this project. Direct and generated.
  out << kIndent << q{"-iquote"} << ", " << q{"."} << ",\n";
  out << kIndent << q{"-iquote"} << ", " << q{"bazel-bin"} << ",\n";

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
            current_package.FullyQualifiedFile(workspace, inc_dir);
          if (!already_seen.insert(inc_path).second) {
            continue;
          }
          out << kIndent << q{"-iquote"} << ", " << q{inc_path} << ",\n";
        }

        // bazel generates virtual include dirs when "include_prefix" is set.
        if (!details.include_prefix.empty()) {
          // TODO: this might be different for external and not. Right now
          // we're focused on external projects, such as protobuf that seems
          // to use this feature.
          const std::string_view external_project = target->package.project;
          const std::string_view target_path = target->package.path;
          const std::string virt_incdir = absl::StrCat(
            "bazel-bin/external/", external_project.substr(1), "/", target_path,
            "/_virtual_includes/", target->target_name);
          if (already_seen.insert(virt_incdir).second) {
            out << kIndent << q{"-iquote"} << ", " << q{virt_incdir} << ",\n";
          }
        }

        // If we depend on anything that looks like protobuf, apply this hack.
        ProtobufAnyPbHack(*target, &already_seen, kIndent, out);

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
          out << kIndent << q{"-iquote"} << ", " << q{ext_dir->path()} << ",\n";

          // Generated files
          const std::string gen_inc =
            absl::StrCat("bazel-bin/external/", external_project.substr(1));
          out << kIndent << q{"-iquote"} << ", " << q{gen_inc} << ",\n";
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
  // headers (which would require to recusively follow all its dependencies),
  // let's just extract all external projects ever used and prepare them
  // as one include blob.
  // More robust currently, but should probably be more specific per file,
  // once we know what we're doing :)
  const std::string external_inc_json = CollectGlobalFlagsAndIncDirs(project);

  std::set<std::string> already_written;
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
                                cwd, external_inc_json, &already_written, out);
      });
  }
  out << "]\n";
}
}  // namespace bant
