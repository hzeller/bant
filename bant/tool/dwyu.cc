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

#include "bant/tool/dwyu.h"

#include <array>
#include <cstddef>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "bant/explore/header-providers.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/tool/dwyu-internal.h"
#include "bant/tool/edit-callback.h"
#include "bant/types-bazel.h"
#include "bant/util/file-utils.h"
#include "bant/util/stat.h"
#include "re2/re2.h"

static constexpr bool kNoisy = false;

// Looking for source files directly in the source tree, but if not found
// in the various locations generated files could be.
#define LINK_PREFIX "bazel-"
// clang-format off
static constexpr std::string_view kSourceLocations[] = {
  "",
  (LINK_PREFIX "out/host/bin/"),
  (LINK_PREFIX "bin/"),
  (LINK_PREFIX "genfiles/"),  // Before bazel 1.1
};
// clang-format on
#undef LINK_PREFIX

namespace bant {

// Given a header file, check if it is in the list. Take possible prefix
// into account.
bool IsHeaderInList(std::string_view header,
                    const std::vector<std::string_view> &list,
                    std::string_view prefix_path) {
  // The list items are provided without the full path in the cc_library(),
  // so we always need to prepend that prefix_path.
  for (const std::string_view list_item : list) {
    if (header.ends_with(list_item) &&  // cheap first test before strcat
        absl::StrCat(prefix_path, "/", list_item) == header) {
      return true;
    }
  }
  return false;
}

DWYUGenerator::DWYUGenerator(Session &session, const ParsedProject &project,
                             EditCallback emit_deps_edit)
    : session_(session),
      project_(project),
      emit_deps_edit_(std::move(emit_deps_edit)) {
  Stat &stats = session_.GetStatsFor("DWYU preparation", "indexed targets");
  const ScopedTimer timer(&stats.duration);

  headers_from_libs_ = ExtractHeaderToLibMapping(project, session.info());
  files_from_genrules_ = ExtractGeneratedFromGenrule(project, session.info());
  InitKnownLibraries();
  stats.count = known_libs_.size();
}

void DWYUGenerator::CreateEditsForTarget(
  Stat &stats, const BazelTarget &self, const query::Result &target,
  const ParsedBuildFile &parsed_package) {
  // Looking at the include files the sources reference, map these back
  // to the dependencies that provide them: these are the deps we
  // needed.
  bool all_header_deps_known = true;
  auto sources = query::ExtractStringList(target.srcs_list);
  query::AppendStringList(target.hdrs_list,
                          sources);  // headers as well.

  auto deps_needed = DependenciesForIncludes(stats, self, parsed_package,  //
                                             sources, &all_header_deps_known);

  // Check all the dependencies that the build target requested and
  // verify we actually need them. If not: remove.
  const auto deps = query::ExtractStringList(target.deps_list);
  for (const std::string_view dependency_target : deps) {
    auto requested_target = BazelTarget::ParseFrom(dependency_target,  //
                                                   self.package);
    if (!requested_target.has_value()) {
      parsed_package.source.Loc(session_.info(), dependency_target)
        << " Invalid target name '" << dependency_target << "'\n";
      continue;
    }

    // Strike off the dependency requested in the build file from the
    // dependendencies we independently determined from the #includes.
    // If it is not on that list, it is a canidate for removal.
    const bool requested_needed = deps_needed.erase(*requested_target);

    const bool potential_remove_suggestion_safe =
      all_header_deps_known && !IsAlwayslink(*requested_target);

    // Emit the edits.
    if (!requested_needed) {
      if (potential_remove_suggestion_safe) {
        emit_deps_edit_(EditRequest::kRemove, self, dependency_target, "");
      } else if (!all_header_deps_known && session_.verbose() && kNoisy) {
        parsed_package.source.Loc(session_.info(), dependency_target)
          << ": Probably not needed " << dependency_target
          << ", but can't safely remove: not all headers accounted for.\n";
      }
    }
  }

  // Now, if there is still something we need, add them.
  for (const BazelTarget &need_add : deps_needed) {
    if (CanSee(self, need_add)) {
      emit_deps_edit_(EditRequest::kAdd, self, "",
                      need_add.ToStringRelativeTo(self.package));
    } else if (session_.verbose() && kNoisy) {
      parsed_package.source.Loc(session_.info(), target.name)
        << ": Would add " << need_add << ", but not visible\n";
    }
  }
}

void DWYUGenerator::CreateEditsForPattern(const BazelPattern &pattern) {
  Stat &stats = session_.GetStatsFor("Grep'ed", "sources");

  const ScopedTimer timer(&stats.duration);
  for (const auto &[_, parsed_package] : project_.ParsedFiles()) {
    const BazelPackage &current_package = parsed_package->package;
    if (!pattern.Match(current_package)) {
      continue;
    }
    query::FindTargets(
      parsed_package->ast, {"cc_library", "cc_binary", "cc_test"},
      [&](const query::Result &target) {
        auto self = BazelTarget::ParseFrom(absl::StrCat(":", target.name),
                                           current_package);
        if (!self.has_value() || !pattern.Match(*self)) {
          return;
        }

        CreateEditsForTarget(stats, *self, target, *parsed_package);
      });
  }
}

// Open the given file an return an line-indexed content or nullptr if file
// not found.
std::optional<DWYUGenerator::SourceFile> DWYUGenerator::TryOpenFile(
  std::string_view source_file) {
  SourceFile result;
  // File could come from multiple locations, primary or generated.
  result.is_generated = false;
  std::optional<std::string> src_content;
  for (const std::string_view search_path : kSourceLocations) {
    result.path = absl::StrCat(search_path, source_file);
    src_content = ReadFileToString(FilesystemPath(result.path));
    if (src_content.has_value()) {
      result.content = std::move(*src_content);
      return result;
    }
    result.is_generated = true;  // Only the first
  }
  return std::nullopt;
}

// We can only confidently remove a target if we actually know about its
// existence in the project. If not, be cautious.
void DWYUGenerator::InitKnownLibraries() {
  for (const auto &[_, parsed_package] : project_.ParsedFiles()) {
    const BazelPackage &current_package = parsed_package->package;
    query::FindTargets(parsed_package->ast,
                       {"cc_library", "cc_proto_library"},  //
                       [&](const query::Result &target) {
                         auto self = BazelTarget::ParseFrom(
                           absl::StrCat(":", target.name), current_package);
                         if (!self.has_value()) {
                           return;
                         }
                         known_libs_.insert({*self, target});
                       });
  }
}

bool DWYUGenerator::IsAlwayslink(const BazelTarget &target) const {
  auto found = known_libs_.find(target);
  if (found == known_libs_.end()) return true;  // Unknown ? Be conservative.
  // TODO: follow all libs we depend on ?
  return found->second.alwayslink;
}

bool DWYUGenerator::CanSee(const BazelTarget &target,
                           const BazelTarget &dep) const {
  auto found = known_libs_.find(dep);
  if (found == known_libs_.end()) return true;  // Unknown ? Be Bold.
  const List *visibility_list = found->second.visibility;
  if (!visibility_list) return true;
  for (Node *entry : *visibility_list) {
    const Scalar *str = entry->CastAsScalar();
    if (!str) continue;
    auto vis_or = BazelPattern::ParseVisibility(str->AsString(), dep.package);
    if (!vis_or.has_value()) continue;
    if (vis_or->Match(target)) {
      return true;
    }
  }
  return false;
}

// Given the sources, grep for headers it uses and resolve their defining
// dependency targets.
// Report in "all_headers_accounted_for", that we found
// a library for each of the headers we have seen.
// This is important as only then we can confidently suggest removals in that
// target.
std::set<BazelTarget> DWYUGenerator::DependenciesForIncludes(
  Stat &stats, const BazelTarget &target, const ParsedBuildFile &build_file,
  const std::vector<std::string_view> &sources,
  bool *all_headers_accounted_for) {
  std::ostream &info_out = session_.info();
  size_t total_size = 0;
  std::set<BazelTarget> result;
  for (const std::string_view src_name : sources) {
    const std::string source_file = build_file.package.QualifiedFile(src_name);
    auto source_content = TryOpenFile(source_file);
    if (!source_content.has_value()) {
      build_file.source.Loc(info_out, src_name)
        << " Can not read source '" << source_file << "' referenced in "
        << target.ToString() << " Missing ? Generated ?\n";
      *all_headers_accounted_for = false;
      continue;
    }

    ++stats.count;
    total_size += source_content->content.size();
    NamedLineIndexedContent source(source_content->path,
                                   source_content->content);
    const auto pound_includes = ExtractCCIncludes(&source);

    // Now for all includes, we need to make sure we can account for it.
    for (const std::string_view inc_file : pound_includes) {
      if (IsHeaderInList(inc_file, sources, target.package.path)) {
        continue;  // Cool, our own list srcs=[...], hdrs=[...]
      }

      // mmh, maybe we included it without the proper prefix ?
      if (IsHeaderInList(inc_file, sources, "")) {
        if (!source_content->is_generated) {  // Only complain if actionable
          source.Loc(info_out, inc_file)
            << inc_file << " header relative to this file. "
            << "Consider FQN relative to project root.\n";
        }
        continue;  // But, anyway, found it in our own sources; accounted for.
      }

      if (const auto found = headers_from_libs_.find(inc_file);
          found != headers_from_libs_.end()) {
        if (found->second != target) {
          result.insert(found->second);  // A target we know is exporting it.
        }
        continue;
      }

      // Maybe include is not provided with path relative to project root ?
      const std::string abs_header = build_file.package.QualifiedFile(inc_file);
      if (const auto found = headers_from_libs_.find(abs_header);
          found != headers_from_libs_.end()) {
        if (!source_content->is_generated) {  // Only complain if actionable
          source.Loc(info_out, inc_file)
            << " " << inc_file << " header relative to this file. "
            << "Consider FQN relative to project root.\n";
        }
        if (found->second != target) {
          result.insert(found->second);
        }
        continue;
      }

      // Everything beyond here, we don't really know where a header is
      // coming from, so need to be careful suggesting removal of any dep.
      *all_headers_accounted_for = false;

      // Not found anywhere, but maybe we can at least report possible source.
      if (const auto found = files_from_genrules_.find(inc_file);
          found != files_from_genrules_.end()) {
        source.Loc(info_out, inc_file)
          << " " << inc_file << " not accounted for; generated by genrule "
          << found->second.ToString() << ", but not "
          << "in hdrs=[...] of any cc_library() we depend on.\n";
        build_file.source.Loc(info_out, src_name)
          << " ... in source of rule " << target.ToString() << "\n";
        continue;
      }

      // More possible checks
      //  - is this part of any library, but only in the srcs=[], but not
      //    in hdrs ? Then suggest to export it in that library.
      //  - is it not mentioned anywhere, but it shows up in the filesystem ?
      //    maybe forgot to add to any library.

      // No luck. Source includes it, but we don't know where it is.
      // Be careful with remove suggestion, so consider 'not accounted for'.
      if (session_.verbose()) {
        // Until we have a glob() implementation, this is pretty noisy at this
        // point. So wrap only show it if verbose enabled.
        source.Loc(info_out, inc_file)
          << " " << inc_file << " unaccounted for; "
          << "glob()'ed ? lib missing ? bazel build needed ?\n";
        build_file.source.Loc(info_out, src_name)
          << " ... in source of rule " << target.ToString() << "\n";
      }
    }
  }

  stats.AddBytesProcessed(total_size);
  return result;
}

std::vector<std::string_view> ExtractCCIncludes(NamedLineIndexedContent *src) {
  static const LazyRE2 kIncRe{
    R"/((?m)("|^\s*#include\s+"([0-9a-zA-Z_/-]+(\.[a-zA-Z]+)*)"))/"};

  // We don't actually understand strings in c++, so we just pretend by
  // toggle ignore whenever we see one.
  bool best_effort_in_nested_quote_toggle = false;
  std::vector<std::string_view> result;
  std::string_view run = src->content();
  std::string_view header_path;
  std::string_view outer;
  while (RE2::FindAndConsume(&run, *kIncRe, &outer, &header_path)) {
    if (outer == "\"") {
      best_effort_in_nested_quote_toggle = !best_effort_in_nested_quote_toggle;
    } else if (!best_effort_in_nested_quote_toggle) {
      result.push_back(header_path);
    }
  }

  if (!result.empty()) {
    // We only need to fill the location_mapper up to the location the last
    // element was found
    const std::string_view range(src->content().begin(),
                                 result.back().end() - src->content().begin());
    src->mutable_line_index()->InitializeFromStringView(range);
  }
  return result;
}

void CreateDependencyEdits(Session &session, const ParsedProject &project,
                           const BazelPattern &pattern,
                           const EditCallback &emit_deps_edit) {
  DWYUGenerator gen(session, project, emit_deps_edit);
  gen.CreateEditsForPattern(pattern);
}
}  // namespace bant
