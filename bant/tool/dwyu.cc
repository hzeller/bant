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

#include "bant/tool/dwyu.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/log/check.h"
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
#include "bant/types.h"
#include "bant/util/file-utils.h"
#include "bant/util/stat.h"
#include "re2/re2.h"

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
        (header == list_item ||
         absl::StrCat(prefix_path, "/", list_item) == header)) {
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

  headers_from_libs_ = ExtractHeaderToLibMapping(project, session.info(),
                                                 /*suffix_index=*/true);
  files_from_genrules_ = ExtractGeneratedFromGenrule(project, session.info());
  InitKnownLibraries();
  stats.count = known_libs_.size();
}

static absl::btree_set<BazelTarget> intersect(
  const absl::btree_set<BazelTarget> &a,
  const absl::btree_set<BazelTarget> &b) {
  absl::btree_set<BazelTarget> result;
  std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                        std::inserter(result, result.begin()));
  return result;
}

// Input is a list of dependency alternative we need: for each header file,
// there are potentially multiple libraries that are providing these,
// the 'alternatives'. So we have a bag of alternative sets.
// Output is a potentially smaller set of smaller alternatives.
static std::vector<absl::btree_set<BazelTarget>> MinimizeDependencySet(
  const std::vector<absl::btree_set<BazelTarget>> &to_reduce) {
  // Find all the sets that intersect, and only remember the intersection.
  // The intersection will be sufficient to satisfy the dependency requirements
  // for both.

  // n^2, but usually pretty small n.
  std::vector<absl::btree_set<BazelTarget>> result;
  std::set<size_t> already_covereed;
  for (size_t i = 0; i < to_reduce.size(); ++i) {
    if (already_covereed.contains(i)) continue;
    already_covereed.insert(i);
    auto current_set = to_reduce[i];
    for (size_t j = i + 1; j < to_reduce.size(); ++j) {
      auto intersection_set = intersect(current_set, to_reduce[j]);
      if (intersection_set.empty()) continue;
      current_set = intersection_set;
      already_covereed.insert(j);
    }
    CHECK_GT(current_set.size(), size_t(0));
    result.push_back(current_set);
  }

  CHECK(already_covereed.size() == to_reduce.size());
  return result;
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
  deps_needed = MinimizeDependencySet(deps_needed);
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
    const auto requested_target = BazelTarget::ParseFrom(dependency_target,  //
                                                         target.package);
    if (!requested_target.has_value()) {
      project_.Loc(session_.info(), dependency_target)
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
        project_.Loc(session_.info(), dependency_target)
          << " in target " << target << ": dependency " << dependency_target
          << " same dependency mentioned multiple times. Run buildifier\n";
      } else {
        project_.Loc(session_.info(), dependency_target)
          << " in target " << target << ": dependency " << dependency_target
          << " provides headers already provided by " << previously
          << " before. Multiple libraries providing the same headers ?\n";
      }
      continue;
    }

    // Looks like we don't need this dependency. But maybe we don't quite know:
    const bool potential_remove_suggestion_safe =
      all_header_deps_known && !IsAlwayslink(*requested_target);

    // Emit the edits.
    if (potential_remove_suggestion_safe) {
      static const LazyRE2 kExcludeVetoUserCommentRe{"#.*keep"};
      const auto line = project_.GetSurroundingLine(dependency_target);
      if (session_.flags().ignore_keep_comment ||
          !RE2::PartialMatch(line, *kExcludeVetoUserCommentRe)) {
        emit_deps_edit_(EditRequest::kRemove, target, dependency_target, "");
      }
    } else if (!all_header_deps_known && session_.flags().verbose > 1) {
      project_.Loc(session_.info(), dependency_target)
        << ": Unsure what " << requested_target->ToString()
        << " provides, but there are also unaccounted headers. Won't remove.\n";
    }
  }

  // Now, if there is still something we need, add them.
  for (const auto &need_add_alternatives : deps_needed) {
    // Only possible to auto-add if there is exactly one alternative.
    if (need_add_alternatives.size() > 1) {
      project_.Loc(session_.info(), details.name)
        << " Can't auto-fix: Referenced headers in " << target
        << " need exactly one of multiple choices\nAlternatives are:\n";
      for (const BazelTarget &target : need_add_alternatives) {
        session_.info() << "\t" << target << "\n";
      }
      continue;
    }

    const BazelTarget &need_add = *need_add_alternatives.begin();
    if (CanSee(target, need_add) && IsTestonlyCompatible(target, need_add)) {
      emit_deps_edit_(EditRequest::kAdd, target, "",
                      need_add.ToStringRelativeTo(target.package));
    } else if (session_.flags().verbose > 1) {
      project_.Loc(session_.info(), details.name)
        << ": Would add " << need_add << ", but not visible\n";
    }
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
                       {"cc_library", "alias",  // The common ones
                        "cc_proto_library", "grpc_cc_library",  // specialized
                        "cc_test"},  // also indexing test for testonly check.
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

bool DWYUGenerator::IsTestonlyCompatible(const BazelTarget &target,
                                         const BazelTarget &dep) const {
  const auto dependency_detail_found = known_libs_.find(dep);
  if (dependency_detail_found == known_libs_.end()) return true;

  const query::Result &dep_detail = dependency_detail_found->second;
  if (!dep_detail.testonly) return true;  // non-testonly always compatible.

  const auto target_detail_found = known_libs_.find(target);
  if (target_detail_found == known_libs_.end()) {
    return true;  // Should not happen, but let's not flag as issue.
  }
  const query::Result &target_detail = target_detail_found->second;
  if (target_detail.testonly || target_detail.rule == "cc_test") {
    return true;  // target and dependency are both tests.
  }

  project_.Loc(session_.info(), target_detail.name)
    << " '" << target << "' is using headers that would be provided by '" << dep
    << "', but the latter is marked testonly, the former not. "
    << "Not adding dependency.\n";
  // TODO: print _what_ headers that is.

  return false;
}

// Visiblity check.
bool DWYUGenerator::CanSee(const BazelTarget &target,
                           const BazelTarget &dep) const {
  auto found = known_libs_.find(dep);
  if (found == known_libs_.end()) return true;  // Unknown ? Be Bold.
  if (!found->second.deprecation.empty()) {
    // Consider a library with a deprecation as not visible.
    return false;
  }

  if (target.package == dep.package) {
    // We can implicitly see all the targets in the same package.
    return true;
  }

  // Somewhat ugly hack: the protobuf library has a protobuf_headers library
  // that does not acctually provide any actual libraries. From the comment
  // there it is there for some shared object building rules; but we should
  // not depend on it, so pretend we can't see it.
  if (dep.target_name == "protobuf_headers") {
    return false;
  }

  List *visibility_list = found->second.visibility;
  if (!visibility_list) return true;
  bool any_valid_visiblity_pattern = false;
  for (Node *entry : *visibility_list) {
    const Scalar *str = entry->CastAsScalar();
    if (!str) continue;
    auto vis_or = BazelPattern::ParseVisibility(str->AsString(), dep.package);
    if (!vis_or.has_value()) continue;
    any_valid_visiblity_pattern = true;
    if (vis_or->Match(target)) {
      return true;
    }
  }
  // There might be variables and other things that we couldn't elaborate.
  // So in case there was not a single pattern we can expand, assume this to
  // be public visibility.
  return !any_valid_visiblity_pattern;
}

std::vector<absl::btree_set<BazelTarget>>
DWYUGenerator::DependenciesNeededBySources(
  const BazelTarget &target, const ParsedBuildFile &build_file,
  const std::vector<std::string_view> &sources,
  bool *all_headers_accounted_for) {
  Stat &source_read_stats = session_.GetStatsFor("read(C++ source)", "sources");
  Stat &source_grep_stats = session_.GetStatsFor("Grep'ed for #inc", "sources");

  std::ostream &info_out = session_.info();
  size_t total_size = 0;

  // Already provided targets we don't need to emit anymore.
  std::set<BazelTarget> already_provided;
  already_provided.insert(target);

  // Log providers if super verbose -vvv
  auto maybe_log = [&](const NamedLineIndexedContent &source,
                       std::string_view inc_file,
                       const absl::btree_set<BazelTarget> &alternatives) {
    if (session_.flags().verbose < 3) return;
    source.Loc(info_out, inc_file) << " #include \"" << inc_file << "\"\n";
    for (const BazelTarget &possible_provider : alternatives) {
      std::string msg;
      if (!CanSee(target, possible_provider)) {
        if (session_.flags().do_color) msg.append("\033[31m");
        msg.append(" (not visible)");
        if (session_.flags().do_color) msg.append("\033[0m");
      }
      source.Loc(info_out, inc_file)
        << "    | " << possible_provider << msg << "\n";
    }
  };

  // Add alternatives to the result we return, but filtering to only
  // include visible targets.
  std::vector<absl::btree_set<BazelTarget>> result;
  auto add_to_result = [&](const absl::btree_set<BazelTarget> &alternatives) {
    // Add all visible targets.
    auto &result_set = result.emplace_back();
    for (const BazelTarget &t : alternatives) {
      if (CanSee(target, t)) {
        result_set.insert(t);
      }
    }

    if (result_set.empty()) {  // Didn't need to add anything: nothing visible
      result.pop_back();
    }
  };

  for (const std::string_view src_name : sources) {
    const std::string source_file =
      build_file.package.FullyQualifiedFile(project_.workspace(), src_name);
    std::optional<DWYUGenerator::SourceFile> source_content;
    {
      const ScopedTimer timer(&source_read_stats.duration);
      source_content = TryOpenFile(source_file);
    }
    if (!source_content.has_value()) {
      project_.Loc(info_out, src_name)
        << " Can not read source '" << source_file << "' for target " << target;
      const auto from_genrule = files_from_genrules_.find(source_file);
      if (from_genrule != files_from_genrules_.end()) {
        info_out << "; Run genrule `bazel build " << from_genrule->second
                 << "` first.\n";
      } else {
        info_out << " -- Missing ?\n";
      }
      *all_headers_accounted_for = false;
      continue;
    }

    // There migth be multiple complaints about various includes found
    // in the same file. If so, only print reference to BUILD file once.
    bool need_in_source_referenced_message = false;

    ++source_read_stats.count;
    ++source_grep_stats.count;
    total_size += source_content->content.size();
    NamedLineIndexedContent source(source_content->path,
                                   source_content->content);
    std::vector<std::string_view> pound_includes;
    {
      const ScopedTimer timer(&source_grep_stats.duration);
      pound_includes = ExtractCCIncludes(&source);
    }
    // Now for all includes, we need to make sure we can account for it.
    for (const std::string_view inc_file : pound_includes) {
      if (IsHeaderInList(inc_file, sources, target.package.path)) {
        continue;  // Cool, our own list srcs=[...], hdrs=[...]
      }

      // mmh, maybe we included it without the proper prefix ?
      if (IsHeaderInList(inc_file, sources, "")) {
        if (!source_content->is_generated) {  // Only complain if actionable
          source.Loc(info_out, inc_file)
            << " " << inc_file << " header relative to this file. "
            << "Consider FQN relative to project root.\n";
          need_in_source_referenced_message = true;
        }
        continue;  // But, anyway, found it in our own sources; accounted for.
      }

      if (const auto &found = FindBySuffix(headers_from_libs_, inc_file);
          found.has_value()) {
        const auto &found_result = found.value();

        // Do some reporting if fuzzy match hit.
        const size_t found_len = found_result.match.length();
        const size_t inc_len = inc_file.length();
        if (found_len != inc_len && session_.flags().verbose > 1) {
          source.Loc(info_out, inc_file)
            << " FYI: instead of '" << inc_file << "' found library that "
            << "provides " << ((found_len < inc_len) ? "shorter" : "longer")
            << " same-suffix path '" << found_result.match << "'\n";
        }
        maybe_log(source, inc_file, *found_result.target_set);
        add_to_result(*found_result.target_set);
        continue;
      }

      // Maybe include is not provided with path relative to project root ?
      const std::string abs_header = build_file.package.QualifiedFile(inc_file);
      if (const auto &found = FindBySuffix(headers_from_libs_, abs_header);
          found.has_value()) {
        if (!source_content->is_generated) {  // Only complain if actionable
          source.Loc(info_out, inc_file)
            << " " << inc_file << " header relative to this file. "
            << "Consider FQN relative to project root.\n";
          need_in_source_referenced_message = true;
        }
        maybe_log(source, inc_file, *found->target_set);
        add_to_result(*found->target_set);
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
        need_in_source_referenced_message = true;
        continue;
      }

      // More possible checks
      //  - is this part of any library, but only in the srcs=[], but not
      //    in hdrs ? Then suggest to export it in that library.
      //  - is it not mentioned anywhere, but it shows up in the filesystem ?
      //    maybe forgot to add to any library.

      // No luck. Source includes it, but we don't know where it is.
      // Be careful with remove suggestion, so consider 'not accounted for'.
      if (session_.flags().verbose) {
        // Until all common reasons why we don't find a provider is resolved,
        // keep this hidden behind verbose.
        source.Loc(info_out, inc_file)
          << " unknown provider for " << inc_file
          << " -- Missing or from non-standard bazel-rule ?\n";
        need_in_source_referenced_message = true;
      }
    }

    if (need_in_source_referenced_message) {
      project_.Loc(info_out, src_name)
        << " ^... in source '" << src_name << "' referenced by "
        << target.ToString() << "\n";
    }
  }

  source_read_stats.AddBytesProcessed(total_size);
  source_grep_stats.AddBytesProcessed(total_size);
  return result;
}

std::vector<std::string_view> ExtractCCIncludes(NamedLineIndexedContent *src) {
  static const LazyRE2 kIncRe{
    R"/((?m)("|^\s*#\s*include\s+"((\.\./)*[0-9a-zA-Z_/+-]+(\.[a-zA-Z]+)*)"))/"};

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

size_t DWYUGenerator::CreateEditsForPattern(const BazelPattern &pattern) {
  size_t matching_patterns = 0;
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
        ++matching_patterns;
        CreateEditsForTarget(*target, details, *parsed_package);
      });
  }
  return matching_patterns;
}

size_t CreateDependencyEdits(Session &session, const ParsedProject &project,
                             const BazelPattern &pattern,
                             const EditCallback &emit_deps_edit) {
  size_t edits_emitted = 0;
  const EditCallback edit_counting_forwarder =
    [&](EditRequest op, const BazelTarget &target,  //
        std::string_view before, std::string_view after) {
      ++edits_emitted;
      emit_deps_edit(op, target, before, after);
    };
  DWYUGenerator gen(session, project, edit_counting_forwarder);
  const size_t target_count = gen.CreateEditsForPattern(pattern);
  session.info() << "Checked DWYU on " << target_count << " targets.";
  if (edits_emitted) {
    session.info() << " Emitted " << edits_emitted << " edits.";
  }
  session.info() << "\n";
  return edits_emitted;
}
}  // namespace bant
