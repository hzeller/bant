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
//   - remove prefix include path (e.g. jsonhpp)
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
#define BUILDOZER

// Looking for source files directly in the source tree, but if not found
// in the various locations generated files could be.
static constexpr std::string_view kSourceLocations[] = {
  "", "bazel-out/host/bin/", "bazel-bin/"};

namespace bant {
namespace {
// TODO: this might be a useful utiltiy function to lift out.
void ExtractList(List *list, std::vector<std::string_view> &append_to) {
  if (list == nullptr) return;
  for (Node *n : *list) {
    Scalar *scalar = n->CastAsScalar();
    if (!scalar) continue;
    if (std::string_view str = scalar->AsString(); !str.empty()) {
      append_to.push_back(str);
    }
  }
}

// Given the sources, grep for headers it uses and resolve thir defining
// dependency targets.
std::set<BazelTarget> TargetsForIncludes(
  const BazelTarget &target_self, const FileContent &context,
  const std::vector<std::string_view> &sources,
  const HeaderToTargetMap &header2dep,  //
  bool *all_headers_accounted_for, std::ostream &out) {
  std::set<BazelTarget> targets_needed;
  for (std::string_view s : sources) {
    const std::string source_file = context.package.QualifiedFile(s);
    std::optional<std::string> src_content;
    for (std::string_view search_path : kSourceLocations) {
      src_content = ReadFileToString(absl::StrCat(search_path, source_file));
      if (src_content.has_value()) break;
    }
    if (!src_content.has_value()) {
      // Nothing we can do about this for now. These are probably
      // coming from some generated sources. TODO: check 'out's from genrules
      // Since we don't know what they include, influences remove confidences.
      std::cerr << context.filename << ":" << context.line_columns.GetRange(s)
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
        std::cerr << context.filename << ":" << context.line_columns.GetRange(s)
                  << " '" << source_file << "'has #include \"" << header
                  << "\" - not sure where from.\n";
#endif
        *all_headers_accounted_for = false;
        continue;
      }
      if (found->second == target_self) continue;
      targets_needed.insert(found->second);
    }
  }
  return targets_needed;
}

// We need to exclude alwayslink libraries from remove suggestions.
// Until we have a full dependency graph we can walk, gather this info manually.
std::set<BazelTarget> ExtractAlwaysLinkLibs(const ParsedProject &project) {
  std::set<BazelTarget> result;
  using query::TargetParameters;
  for (const auto &[_, parsed_package] : project.file_to_ast) {
    const BazelPackage &current_package = parsed_package.package;
    query::FindTargets(parsed_package.ast, {"cc_library"},  //
                       [&](const TargetParameters &target) {
                         if (!target.alwayslink) {
                           return;  // only interested in alwayslink
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

void PrintDependencyEdits(const ParsedProject &project, std::ostream &out) {
  const HeaderToTargetMap header2dep = ExtractHeaderToLibMapping(project);
  const std::set<BazelTarget> alwaylink_libs = ExtractAlwaysLinkLibs(project);
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
        ExtractList(target.srcs_list, sources);
        ExtractList(target.hdrs_list, sources);
        auto targets_needed = TargetsForIncludes(*self, parsed_package,      //
                                                 sources, header2dep,        //
                                                 &confident_suggest_remove,  //
                                                 out);

        // Check all the dependencies build target requested, but doesnt't need.
        std::vector<std::string_view> deps;
        ExtractList(target.deps_list, deps);
        for (std::string_view dependency_target : deps) {
          auto requested_target = BazelTarget::ParseFrom(dependency_target,  //
                                                         current_package);
          if (!requested_target.has_value()) {
            out << parsed_package.filename << ":"
                << parsed_package.line_columns.GetRange(dependency_target)
                << " Invalid target name '" << dependency_target << "'\n";
            continue;
          }
          size_t requested_was_needed = targets_needed.erase(*requested_target);
          if (!requested_was_needed && confident_suggest_remove &&
              !alwaylink_libs.contains(*requested_target)) {
#ifdef BUILDOZER
            out << "buildozer 'remove deps " << dependency_target << "' "
                << *self << "\n";
#else
            out << parsed_package.filename << ":"
                << parsed_package.line_columns.GetRange(dependency_target)
                << " REMOVE: '" << *requested_target << " in :" << target.name
                << "\n";
#endif
          }
        }

      // Now, if there is still something in the 'needs'-set, suggest adding.

      // We need to suggest where to add. Maybe simply at the begin of the
      // List, but we don't have that directly. So we look for the first
      // dependency if there is any and use that as position. Or the
      // target itself.
#ifndef BUILDOZER
        auto insert_reference = !deps.empty() ? deps[0] : target.name;
        auto add_loc = parsed_package.line_columns.GetRange(insert_reference);
#endif
        for (const BazelTarget &need_add : targets_needed) {
#ifdef BUILDOZER
          out << "buildozer 'add deps "
              << need_add.ToStringRelativeTo(current_package) << "' " << *self
              << "\n";
#else
          out << parsed_package.filename << ":" << add_loc << " ADD...: '"
              << need_add.ToStringRelativeTo(current_package)
              << "' in :" << target.name << "\n";
#endif
        }
      });
  }
}
}  // namespace bant
