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
//   - some won't find includes yet because they are the result of a glob()
//     operation, e.g. gtest/gtest.h.
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

#include "absl/container/btree_set.h"
#include "absl/strings/str_cat.h"
#include "bant/explore/aliased-by.h"
#include "bant/explore/header-providers.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/tool/dwyu-internal.h"
#include "bant/tool/edit-callback.h"
#include "bant/types-bazel.h"
#include "bant/types.h"
#include "bant/util/file-utils.h"
#include "bant/util/stat.h"
#include "re2/re2.h"

// Used for messages that are a little bit too noisy right now.
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
  aliased_by_ = ExtractAliasedBy(project);
  InitKnownLibraries();
  stats.count = known_libs_.size();
}

void DWYUGenerator::CreateEditsForTarget(const BazelTarget &target,
                                         const query::Result &details,
                                         const ParsedBuildFile &build_file) {
  // Looking at the include files the sources reference, map these back
  // to the dependencies that provide them: these are the deps we
  // needed.
  bool all_header_deps_known = true;

  // Collect sources and headers provided by this library.
  auto sources = query::ExtractStringList(details.srcs_list);
  query::AppendStringList(details.hdrs_list, sources);

  // Grep for all includes they use to determine which deps we need
  auto deps_needed = DependenciesNeededBySources(target, build_file, sources,
                                                 &all_header_deps_known);
  OneToOne<BazelTarget, BazelTarget> checked_off_by;

  auto IsNeededInSourcesAndCheckOff = [&](const BazelTarget &target) -> bool {
    for (auto it = deps_needed.begin(); it != deps_needed.end(); ++it) {
      if (it->contains(target)) {
        for (const BazelTarget &check : *it) {
          checked_off_by.insert({check, target});  // remember what checked off.
        }
        deps_needed.erase(it);  // alternatives satisifed. Remove.
        return true;
      }
    }
    return false;
  };

  // Check all the dependencies that the build target requested and strike
  // them off the 'deps_needed' list.
  // Everything deps_needed
  // verify we actually need them. If not: remove.
  const auto deps = query::ExtractStringList(details.deps_list);
  for (const std::string_view dependency_target : deps) {
    auto requested_target = BazelTarget::ParseFrom(dependency_target,  //
                                                   target.package);
    if (!requested_target.has_value()) {
      build_file.source.Loc(session_.info(), dependency_target)
        << " Invalid target name '" << dependency_target << "'\n";
      continue;
    }

    // Strike off the dependency requested in the build file from the
    // dependendencies we independently determined from the #includes.
    // If it is not on that list, it is a canidate for removal.
    if (IsNeededInSourcesAndCheckOff(*requested_target)) {
      continue;
    }

    if (checked_off_by.contains(*requested_target)) {
      const BazelTarget &previously = checked_off_by[*requested_target];
      if (previously == *requested_target) {
        build_file.source.Loc(session_.info(), dependency_target)
          << ": " << dependency_target
          << " same dependency mentioned multiple times. Run buildifier\n";
      } else {
        build_file.source.Loc(session_.info(), dependency_target)
          << ": " << dependency_target
          << " provides headers already provided in dependency " << previously
          << " before. Multiple libraries providing the same headers ?\n";
      }
      continue;
    }

    // Looks like we don't need this dependency. But maybe we don't quite know:
    const bool potential_remove_suggestion_safe =
      all_header_deps_known && !IsAlwayslink(*requested_target);

    // Emit the edits.
    if (potential_remove_suggestion_safe) {
      emit_deps_edit_(EditRequest::kRemove, target, dependency_target, "");
    } else if (!all_header_deps_known && session_.verbose() && kNoisy) {
      build_file.source.Loc(session_.info(), dependency_target)
        << ": Probably not needed " << dependency_target
        << ", but can't safely remove: not all headers accounted for.\n";
    }
  }

  // Now, if there is still something we need, add them.
  for (const auto &need_add_alternatives : deps_needed) {
    // Only possible to auto-add if there is exactly one alternative.
    if (need_add_alternatives.size() > 1) {
      build_file.source.Loc(session_.info(), details.name)
        << " Can't auto-decide: Referenced headers in " << target
        << " need exactly one of multiple choices\nAlternatives are:\n";
      for (const BazelTarget &target : need_add_alternatives) {
        session_.info() << "\t" << target << "\n";
      }
      continue;
    }

    const BazelTarget &need_add = *need_add_alternatives.begin();
    if (CanSee(target, need_add)) {
      emit_deps_edit_(EditRequest::kAdd, target, "",
                      need_add.ToStringRelativeTo(target.package));
    } else if (session_.verbose() && kNoisy) {
      build_file.source.Loc(session_.info(), details.name)
        << ": Would add " << need_add << ", but not visible\n";
    }
  }
}

void DWYUGenerator::CreateEditsForPattern(const BazelPattern &pattern) {
  for (const auto &[_, parsed_package] : project_.ParsedFiles()) {
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

        CreateEditsForTarget(*target, details, *parsed_package);
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
  if (target.package == dep.package) {
    // We can implicitly see all the targets in the same package.
    return true;
  }
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

std::vector<absl::btree_set<BazelTarget>>
DWYUGenerator::DependenciesNeededBySources(
  const BazelTarget &target, const ParsedBuildFile &build_file,
  const std::vector<std::string_view> &sources,
  bool *all_headers_accounted_for) {
  Stat &grep_stats = session_.GetStatsFor("Grep'ed", "sources");
  const ScopedTimer timer(&grep_stats.duration);

  std::ostream &info_out = session_.info();
  size_t total_size = 0;
  std::vector<absl::btree_set<BazelTarget>> result;

  // Already provided targets we don't need to emit anymore.
  std::set<BazelTarget> already_provided;
  already_provided.insert(target);

  auto add_to_result = [&](const absl::btree_set<BazelTarget> &alternatives) {
    bool any_already_provided = false;
    for (const BazelTarget &t : alternatives) {
      any_already_provided |= !already_provided.insert(t).second;
    }
    if (any_already_provided) return;

    auto &result_set = result.emplace_back();
    // Add all visible alternatives as well as all aliases pointing to these.
    for (const BazelTarget &t : alternatives) {
      if (CanSee(target, t)) {
        result_set.insert(t);
      }
      const auto &found_alias = aliased_by_.find(t);
      if (found_alias != aliased_by_.end()) {
        for (const BazelTarget &alias : found_alias->second) {
          if (CanSee(target, alias)) {
            result_set.insert(alias);
          }
        }
      }
    }
    if (result_set.empty()) {  // No visible targets.
      result.pop_back();
    }
  };

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

    ++grep_stats.count;
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
        add_to_result(found->second);
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
        add_to_result(found->second);
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

  grep_stats.AddBytesProcessed(total_size);
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
