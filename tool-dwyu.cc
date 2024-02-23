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

// TODO:
//   - some want find includes yet because they are the result of a glob()
//     operation, e.g. gtest/gtest.h.
//   - Don't add things that are not visible (e.g. absl vlog_is_on)
//   - generated sources: add heuristic. Check out = "..." fields. Or
//     proto buffers.
//

#include "tool-dwyu.h"

#include <array>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "file-utils.h"
#include "project-parser.h"
#include "query-utils.h"
#include "re2/re2.h"
#include "tool-header-providers.h"
#include "types-bazel.h"

// #define ADD_UNKNOWN_SOURCE_MESSAGE

// gtest_main should never be considered removable. However, depending on
// if tests were compiled before, we might not even see it in
// bazel-${project}/external. Figure out where to get a stable list of externals
#define BANT_GTEST_HACK 1

// Looking for source files directly in the source tree, but if not found
// in the various locations generated files could be.
static constexpr std::string_view kSourceLocations[] = {
  "", "bazel-out/host/bin/", "bazel-bin/"};

namespace bant {
namespace {
// Given the sources, grep for headers it uses and resolve thir defining
// dependency targets.
// Report in "all_headers_accounted_for", that we found
// a library for each of the headers we have seen.
// This is important as only then we can confidently suggest removals in that
// target.
std::set<BazelTarget> TargetsForIncludes(
  const BazelTarget &target_self, const FileContent &context,
  const std::vector<std::string_view> &sources,
  const HeaderToTargetMap &header2dep, bool *all_headers_accounted_for,
  std::ostream &info_out) {
  std::set<BazelTarget> result;
  for (std::string_view s : sources) {
    const std::string source_file = context.package.QualifiedFile(s);

    // File could be in multiple locations, primary or generated. Use first.
    std::optional<std::string> src_content;
    for (std::string_view search_path : kSourceLocations) {
      src_content = ReadFileToString(absl::StrCat(search_path, source_file));
      if (src_content.has_value()) break;
    }
    if (!src_content.has_value()) {
      // Nothing we can do about this for now. These are probably
      // coming from some generated sources. TODO: check 'out's from genrules
      // Since we don't know what they include, influences remove confidences.
      info_out << context.filename << ":" << context.line_columns.GetRange(s)
               << " Can not read '" << source_file << "' referenced in "
               << target_self.ToString() << " Probably generated ?\n";
      *all_headers_accounted_for = false;
      continue;
    }

    auto headers = ExtractCCIncludes(*src_content);
    for (const std::string &header : headers) {
      auto found = header2dep.find(header);
      if (found == header2dep.end()) {
        // There is a header we don't know where it is coming from.
        // Need to be careful with remove suggestion.
#ifdef ADD_UNKNOWN_SOURCE_MESSAGE
        info_out << context.filename << ":" << context.line_columns.GetRange(s)
                 << " '" << source_file << "' has #include \"" << header
                 << "\" - not sure where from.\n";
#endif
        *all_headers_accounted_for = false;
        continue;
      }
      if (found->second == target_self) continue;
      result.insert(found->second);
    }
  }
  return result;
}

// We can only confidently remove a target if we actually know about its
// existence in the project. If not, be cautious.
std::set<BazelTarget> ExtractKnownLibraries(const ParsedProject &project) {
  std::set<BazelTarget> result;
  using query::TargetParameters;
  for (const auto &[_, parsed_package] : project.file_to_ast) {
    const BazelPackage &current_package = parsed_package.package;
    query::FindTargets(parsed_package.ast, {"cc_library"},  //
                       [&](const TargetParameters &target) {
                         if (target.alwayslink) {
                           // Don't include always-link targets: this makes
                           // sure they are not accidentally removed.
                           return;
                         }
                         auto self = BazelTarget::ParseFrom(
                           absl::StrCat(":", target.name), current_package);
                         if (!self.has_value()) {
                           return;
                         }
                         result.insert(*self);
                       });
  }
  return result;
}
}  // namespace

std::vector<std::string> ExtractCCIncludes(std::string_view content) {
  static const LazyRE2 kIncRe{
    R"/((?m)^\s*#include\s+"([0-9a-zA-Z_/-]+\.[a-zA-Z]+)")/"};

  std::vector<std::string> result;
  std::string header_path;
  while (RE2::FindAndConsume(&content, *kIncRe, &header_path)) {
    result.push_back(header_path);
  }
  return result;
}

void PrintDependencyEdits(const ParsedProject &project, std::ostream &out,
                          std::ostream &info_out) {
  const HeaderToTargetMap header2dep =
    ExtractHeaderToLibMapping(project, info_out);
  const std::set<BazelTarget> known_libs = ExtractKnownLibraries(project);

  using query::TargetParameters;
  for (const auto &[_, parsed_package] : project.file_to_ast) {
    if (!parsed_package.package.project.empty()) {
      continue;  // Only interested in our project, not the externals
    }
    const BazelPackage &current_package = parsed_package.package;
    query::FindTargets(
      parsed_package.ast, {"cc_library", "cc_binary", "cc_test"},
      [&](const TargetParameters &target) {
        auto self = BazelTarget::ParseFrom(absl::StrCat(":", target.name),
                                           current_package);
        if (!self.has_value()) {
          return;
        }

        bool confident_suggest_remove = true;
        std::vector<std::string_view> sources;
        query::ExtractStringList(target.srcs_list, sources);
        query::ExtractStringList(target.hdrs_list, sources);
        auto targets_needed = TargetsForIncludes(*self, parsed_package,      //
                                                 sources, header2dep,        //
                                                 &confident_suggest_remove,  //
                                                 info_out);

        // Check all the dependencies build target requested, but doesnt't need.
        std::vector<std::string_view> deps;
        query::ExtractStringList(target.deps_list, deps);
        for (std::string_view dependency_target : deps) {
          if (!BazelTarget::LooksWellformed(dependency_target)) {
            info_out << parsed_package.filename << ":"
                     << parsed_package.line_columns.GetRange(dependency_target)
                     << " target \"" << dependency_target
                     << "\": no '// or ':' prefix. Consider canonicalizing.\n";
          }
          auto requested_target = BazelTarget::ParseFrom(dependency_target,  //
                                                         current_package);
          if (!requested_target.has_value()) {
            info_out << parsed_package.filename << ":"
                     << parsed_package.line_columns.GetRange(dependency_target)
                     << " Invalid target name '" << dependency_target << "'\n";
            continue;
          }
          size_t requested_was_needed = targets_needed.erase(*requested_target);
          if (!requested_was_needed && confident_suggest_remove &&
              known_libs.contains(*requested_target)) {
            out << "buildozer 'remove deps " << dependency_target << "' "
                << *self << "\n";
          }
        }

        // Now, if there is still something in the 'needs'-set, suggest adding.

        // We need to suggest where to add. Maybe simply at the begin of the
        // List, but we don't have that directly. So we look for the first
        // dependency if there is any and use that as position. Or the
        // target itself.
        for (const BazelTarget &need_add : targets_needed) {
          out << "buildozer 'add deps "
              << need_add.ToStringRelativeTo(current_package) << "' " << *self
              << "\n";
        }
      });
  }
}
}  // namespace bant
