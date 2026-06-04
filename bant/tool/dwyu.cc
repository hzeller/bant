// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
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
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "bant/explore/header-providers.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/named-content.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/source-locator.h"
#include "bant/session.h"
#include "bant/tool/dwyu-internal.h"
#include "bant/tool/edit-callback.h"
#include "bant/tool/preprocess-utils.h"
#include "bant/types-bazel.h"
#include "bant/types.h"
#include "bant/util/file-utils.h"
#include "bant/util/hyperlink-builder.h"
#include "bant/util/stat.h"
#include "bant/util/term-color.h"
#include "bant/workspace.h"
#include "re2/re2.h"

//
// This file is a dense bowl of spaghetti as a result of ad-hoc adding features.
// Refactor.
//

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
static bool IsHeaderInList(std::string_view header,
                           const std::vector<std::string_view> &list,
                           std::string_view prefix_path) {
  // The list items are provided without the full path in the cc_library(),
  // so we always need to prepend that prefix_path.
  for (std::string_view list_item : list) {
    if (list_item.empty()) continue;  // should not happen.
    // TODO: thid colon detection is half-assed and only works for one
    // sub-case. What we actually want to do
    // is to package QualifiedFile() the sources as well as the prefix path,
    // then do the comparisons with the absolute from package path filenames.
    // including all the prefix string comparisons.
    if (list_item[0] == ':') list_item.remove_prefix(1);

    if ((list_item.ends_with(header) ||
         header.ends_with(list_item)) &&  // cheap first test before strcat
        (header == list_item ||
         absl::StrCat(prefix_path, "/", list_item) == header ||
         absl::StrCat(prefix_path, "/", header) == list_item)) {
      return true;
    }
  }
  return false;
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

// Open the given file an return an line-indexed content or nullptr if file
// not found.
std::optional<DWYUGenerator::SourceFile> DWYUGenerator::TryOpenFile(
  std::string_view source_file, Stat &read_stats) {
  SourceFile result;
  // File could come from multiple locations, primary or generated.
  result.is_generated = false;
  std::optional<std::string> src_content;
  for (const std::string_view search_path : kSourceLocations) {
    result.path = absl::StrCat(search_path, source_file);
    src_content =
      ReadFileToStringUpdateStat(FilesystemPath(result.path), read_stats);
    if (src_content.has_value()) {
      result.content = std::move(*src_content);
      return result;
    }
    result.is_generated = true;  // Only the first in list is direct source
  }
  return std::nullopt;
}

// class DWYUGenerator declared in dwyu-internal.h
// We can only confidently remove a target if we actually know about its
// existence in the project. If not, be cautious.
void DWYUGenerator::InitKnownLibraries() {
  for (const auto &[_, parsed_package] : project_.ParsedFiles()) {
    const BazelPackage &current_package = parsed_package->package;
    query::FindTargets(parsed_package->ast,
                       {"cc_library", "alias",  // The common ones
                        "cc_proto_library", "grpc_cc_library",  // specialized
                        "proto_library",                        // proto DWYU
                        "cc_test"},  // also indexing test for testonly check.
                       [&](const query::Result &target) {
                         auto self =
                           current_package.QualifiedTarget(target.name);
                         if (!self.has_value()) {
                           return;
                         }
                         known_libs_.insert({*self, target});
                       });
  }
}

// Various predicates to check
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

  Loc(project_, target_detail.name)
    << " '" << target << "' is using headers that would be provided by '" << dep
    << "', but the latter is marked testonly, the former not. "
    << "Not adding dependency.\n";
  // TODO: print _what_ headers that is.

  return false;
}

// Visiblity check.
std::optional<std::string_view> DWYUGenerator::DeprecationReason(
  const BazelTarget &target) const {
  const auto found = known_libs_.find(target);
  if (found != known_libs_.end() && !found->second.deprecation.empty()) {
    return found->second.deprecation;
  }
  return std::nullopt;
}

bool DWYUGenerator::CanSee(const BazelTarget &target, const BazelTarget &dep,
                           std::string *msg) const {
  const auto found = known_libs_.find(dep);
  if (found == known_libs_.end()) return true;  // Unknown ? Be Bold.

  if (target.package == dep.package) {
    // We can implicitly see all the targets in the same package.
    return true;
  }

  // Somewhat ugly hack: the protobuf library has a protobuf_headers library
  // that does not acctually provide any actual libraries. From the comment
  // there it is there for some shared object building rules; but we should
  // not depend on it, so pretend we can't see it.
  if (dep.target_name == "protobuf_headers") {
    if (msg) *msg = "protobuf_headers don't actually provide implementation";
    return false;
  }

  List *visibility_list = found->second.visibility;
  if (!visibility_list) return true;
  bool any_valid_visiblity_pattern = false;
  bool any_non_matching_visibility_pattern = false;
  for (Node *entry : *visibility_list) {
    const Scalar *str = entry->CastAsScalar();
    if (!str) continue;
    auto vis_or = BazelPattern::ParseVisibility(str->AsString(), dep.package);
    if (!vis_or.has_value()) continue;
    any_valid_visiblity_pattern = true;
    if (vis_or->Match(target)) {
      return true;
    }
    if (msg) {
      if (any_non_matching_visibility_pattern) msg->append("; ");
      absl::StrAppend(msg, project_.Loc(str->AsString()), str->AsString(),
                      " visibility not matched");
    }
    any_non_matching_visibility_pattern = true;
  }
  // There might be variables and other things that we couldn't elaborate.
  // So in case there was not a any pattern we can expand, assume this to
  // be public visibility.
  return !any_valid_visiblity_pattern;
}

namespace {
struct Range {
  void Update(int value) {
    if (!initialized) {
      min_value = max_value = value;
      initialized = true;
      return;
    }
    min_value = std::min(min_value, value);
    max_value = std::max(max_value, value);
  }

  int min_value = 0;
  int max_value = 0;
  bool initialized = false;
};
}  // namespace

int DWYUGenerator::GetStratum(const BazelTarget &target) const {
  const std::string_view project = target.package.project;
  if (project.empty()) {
    return VersionedProject::Stratum::kRootProject;
  }
  const auto &entry = project_.workspace().FindEntryByProject(project);
  if (entry == project_.workspace().project_location.end()) {
    return VersionedProject::Stratum::kUnknown;
  }
  return entry->first.stratum;
}

std::optional<DWYUGenerator::SourceFile> DWYUGenerator::ReadSourceForDWYU(
  std::string_view src_name, const BazelTarget &target,
  const ParsedBuildFile &build_file, Stat &read_stats, bool *all_accounted) {
  // TODO: ParsedProject::GetPackageFor() as we might have filenames coming
  // from different packages due to filegroups.
  const std::string source_file =
    build_file.package.FullyQualifiedFile(project_.workspace(), src_name);
  std::optional<SourceFile> source_content;
  source_content = TryOpenFile(source_file, read_stats);
  if (!source_content.has_value()) {
    std::ostream &info_out = session_.info();
    Loc(project_, src_name) << " Can not read source '" << Magenta(session_)
                            << source_file << Norm(session_) << "' for target "
                            << Bold(session_) << target << Norm(session_);
    const auto from_genrule = files_from_genrules_.find(source_file);
    if (from_genrule != files_from_genrules_.end()) {
      info_out << "; Run generating `bazel build " << from_genrule->second
               << "` first.\n";
      // Remember to put out a one-liner log in the end.
      suggested_genrules_to_run_.insert(from_genrule->second);
    } else {
      info_out << " -- Missing ?\n";
    }
    *all_accounted = false;
  }
  return source_content;
}

void DWYUGenerator::LogUnknownProvider(const NamedLineIndexedContent &source,
                                       std::string_view ref_file,
                                       const BazelTarget &target,
                                       std::string_view ref_keyword,
                                       std::string_view extra_info,
                                       bool remember_for_summary = true) {
  if (!session_.MinVerbosity(1)) return;
  if (remember_for_summary) {
    const FileLocation loc = source.GetLocation(ref_file);
    std::stringstream message;
    message << BlueBold(session_) << HyperLinked{session_.linkgen(), loc}
            << Norm(session_) << " in " << Bold(session_) << target
            << Norm(session_);
    header_without_provider_[ref_file].insert(message.str());
  }
  Loc(source, ref_file) << " " << ref_keyword << " \"" << ref_file << "\"\n";
  Loc(source, ref_file) << Red(session_) << "    ?      ^ unknown provider"
                        << Norm(session_) << extra_info << "\n";
}

void DWYUGenerator::AddVisibleAlternatives(
  const BazelTarget &target, const absl::btree_set<BazelTarget> &alternatives,
  std::vector<absl::btree_set<BazelTarget>> &result) {
  absl::btree_set<BazelTarget> visible;
  absl::btree_set<BazelTarget> visible_deprecated;
  for (const BazelTarget &t : alternatives) {
    if (CanSee(target, t, nullptr)) {
      if (DeprecationReason(t).has_value()) {
        visible_deprecated.emplace(t);
      } else {
        visible.emplace(t);
      }
    }
  }
  if (!visible.empty()) {
    result.push_back(std::move(visible));
  } else if (!visible_deprecated.empty()) {
    // If we _only_ have deprecated alternatives, consider them visible.
    result.push_back(std::move(visible_deprecated));
  }
}

void DWYUGenerator::AddVisibleAlternativesWithStratum(
  const BazelTarget &target, const absl::btree_set<BazelTarget> &alternatives,
  std::vector<absl::btree_set<BazelTarget>> &result) {
  Range stratum_range;
  std::vector<BazelTarget> temp_result;
  bool found_non_deprecated = false;
  for (const BazelTarget &t : alternatives) {
    if (CanSee(target, t, nullptr)) {
      const bool is_deprecated = DeprecationReason(t).has_value();
      if (is_deprecated && found_non_deprecated) continue;
      if (!is_deprecated && !found_non_deprecated) {
        // Until we find the first non-deprecated alternative, we also
        // keep deprecated targets as they might be our only chance.
        temp_result.clear();
        stratum_range = Range{};
        found_non_deprecated = true;
      }
      const int stratum = GetStratum(t);
      stratum_range.Update(stratum);
      if (stratum <= stratum_range.min_value) {
        temp_result.emplace_back(t);
      }
    }
  }
  if (temp_result.empty()) return;
  auto &result_set = result.emplace_back();
  for (const BazelTarget &t : temp_result) {
    if (stratum_range.min_value != stratum_range.max_value &&
        GetStratum(t) != stratum_range.min_value) {
      continue;
    }
    result_set.emplace(t);
  }
}

// TODO: the following does a bunch per source file. This should probably
// be encasulated in a struct or class PerSourceFileDWYU that captures
// all the context and has methods such as HeaderMentionedInOwnSources()
// HeaderIsMentionedInOwnSourceWithIncludePath() etc.
// That way we avoid the various unnamed blocks in a huge loop.
std::vector<absl::btree_set<BazelTarget>>
DWYUGenerator::DependenciesNeededBySources(
  const BazelTarget &target, const ParsedBuildFile &build_file,
  const std::vector<std::string_view> &sources,            // srcs, hdrs
  const std::vector<std::string_view> &includes_dir_list,  // includes = []
  bool *all_headers_accounted_for) {
  Stat &source_read_stats =
    session_.GetStatsFor("  - read(C++ source)", "sources");
  Stat &source_grep_stats =
    session_.GetStatsFor("  - Grep'ed for #inc", "sources");

  size_t total_size = 0;

  // Whenever we encounter an issue in the processing of a source, we first
  // add a headline for easier visual navigation in the log.
  bool source_headline_logged_already = false;
  auto maybe_log_source_headline = [&](std::string_view src_name,
                                       const std::string &path,
                                       const BazelTarget &target) {
    if (!session_.MinVerbosity(1)) return;
    if (source_headline_logged_already) return;
    Loc(project_, src_name)
      << Invert(session_) << "[ " << Bold(session_) << path << BoldOff(session_)
      << " include dependency check " << Bold(session_) << "(" << target
      << ") ]" << Norm(session_) << "\n";
    source_headline_logged_already = true;
  };

  // Log providers if super verbose -vvv
  // This shows a after the include all the dependencies that can provide
  // it.
  auto maybe_log = [&](const NamedLineIndexedContent &source,
                       std::string_view inc_file, bool is_bracket_include,
                       const absl::btree_set<BazelTarget> &alternatives) {
    // Nasty code-smell thus always show, even without verbosity.
    if (is_bracket_include) {
      std::string see_also = !session_.MinVerbosity(3) ? " See with -vvv" : "";
      Loc(source, inc_file)
        << " source of " << Bold(session_) << target << Norm(session_)
        << ": #include " << Magenta(session_) << "<" << inc_file << ">"
        << Norm(session_) << " uses <>-bracketed include style. "
        << Red(session_) << "Should use quote-style "
        << "\"" << inc_file << "\" as this header is provided by "
        << "project libraries." << see_also << Norm(session_) << "\n";
    }
    if (!session_.MinVerbosity(3)) return;
    Loc(source, inc_file) << Bold(session_) << " #include "
                          << (is_bracket_include ? '<' : '"') << inc_file
                          << (is_bracket_include ? '>' : '"') << Norm(session_)
                          << "\n";
    for (const BazelTarget &possible_provider : alternatives) {
      std::stringstream msg;
      std::string why;
      if (!CanSee(target, possible_provider, &why)) {
        msg << Red(session_) << " (" << why << ")" << Norm(session_);
      } else if (auto reason = DeprecationReason(possible_provider)) {
        msg << Red(session_) << " (deprecated: " << *reason << ")"
            << Norm(session_);
      }
      Loc(source, inc_file)
        << "    | " << possible_provider << msg.str() << "\n";
    }
  };

  std::vector<absl::btree_set<BazelTarget>> result;
  const bool allow_bracket_includes = session_.flags().allow_bracket_includes;
  for (const std::string_view src_name : sources) {
    source_headline_logged_already = false;
    auto source_content =
      ReadSourceForDWYU(src_name, target, build_file, source_read_stats,
                        all_headers_accounted_for);
    if (!source_content.has_value()) continue;

    ++source_grep_stats.count;
    total_size += source_content->content.size();

    // Here we should create a struct PerSourceFileDWYU getting source_content
    NamedLineIndexedContent source(source_content->path,
                                   source_content->content);
    std::vector<std::string_view> pound_includes;
    {
      const ScopedTimer timer(&source_grep_stats.duration);
      pound_includes = ExtractCCIncludes(&source);
    }
    // Now for all includes, we need to make sure we can account for it.
    for (std::string_view inc_file : pound_includes) {
      const bool is_bracket_include = inc_file[0] == '<';
      if (is_bracket_include && !allow_bracket_includes) continue;

      inc_file = inc_file.substr(1);

      // Possible refactor-name HeaderMentionedInOwnSources()
      if (IsHeaderInList(inc_file, sources, target.package.path)) {
        continue;  // Cool, our own list srcs=[...], hdrs=[...]
      }

      // Possible refactor-name HeaderIsMentionedInOwnSourceWithIncludePath()
      // Check for all include prefices found in includes = [], effectively
      // making includes visible under shorter paths.
      bool found_local_inc = false;
      for (std::string_view src_prefix : includes_dir_list) {
        if (IsHeaderInList(inc_file, sources, src_prefix)) {
          // Only complain if actionable
          if (!source_content->is_generated && session_.MinVerbosity(2)) {
            maybe_log_source_headline(src_name, source_content->path, target);
            Loc(source, inc_file)
              << Bold(session_) << " -I" << src_prefix << Norm(session_)
              << " matched " << Magenta(session_) << inc_file << Norm(session_)
              << " header relative to this package. "
              << "Consider FQN relative to project root.\n";
          }
          // But, anyway, found it in our own sources; accounted for.
          found_local_inc = true;
        }
      }
      if (found_local_inc) continue;

      // Possible refactor-name HeaderIsMentionedInOwnSourcesViaDotInclude()
      // mmh, maybe we included it without the proper prefix, but somewhat
      // assuming it is local ? Some code assumes `-I.`
      std::string_view src_prefix;
      if (auto sl = src_name.find_last_of('/'); sl != std::string_view::npos) {
        src_prefix = src_name.substr(0, sl);
      }
      if (IsHeaderInList(inc_file, sources, src_prefix)) {
        // Only complain if actionable
        if (!source_content->is_generated && session_.MinVerbosity(2)) {
          maybe_log_source_headline(src_name, source_content->path, target);
          Loc(source, inc_file)
            << " prefix " << Bold(session_) << src_prefix << "/"
            << Norm(session_) << " matched " << Magenta(session_) << inc_file
            << Norm(session_) << " header relative to this file. "
            << "Consider FQN relative to project root.\n";
        }
        continue;  // But, anyway, found it in our own sources; accounted for.
      }

      // Possible refactor-name FindDependencyFromHeaderName()
      if (const auto &found = FindBySuffix(headers_from_libs_, inc_file);
          found.has_value()) {
        const auto &found_result = found.value();
        const auto &header_providers = *found_result.target_set;

        // Is it ourselve we found ? (this can happen with messy build files).
        // In that case: move on.
        if (header_providers.contains(target)) continue;

        // Do some reporting if fuzzy match hit.
        const size_t found_len = found_result.match.length();
        const size_t inc_len = inc_file.length();
        if (found_len != inc_len && session_.MinVerbosity(2)) {
          maybe_log_source_headline(src_name, source_content->path, target);
          Loc(source, inc_file)
            << " FYI: instead of '" << Magenta(session_) << inc_file
            << Norm(session_) << "' found library that "
            << "provides " << ((found_len < inc_len) ? "shorter" : "longer")
            << " same-suffix path '" << Magenta(session_) << found_result.match
            << Norm(session_) << "' ( "
            << absl::StrJoin(header_providers, " | ") << " )\n";
        }
        if (session_.MinVerbosity(3)) {
          maybe_log_source_headline(src_name, source_content->path, target);
        }
        maybe_log(source, inc_file, is_bracket_include,
                  *found_result.target_set);
        AddVisibleAlternativesWithStratum(target, header_providers, result);
        continue;
      }

      // Possible refactor-name FindDependencyFromHeaderNameFuzzyDirMatch()
      // Maybe include is not provided with path relative to project root ?
      const std::string abs_header = build_file.package.QualifiedFile(inc_file);
      if (const auto &found = FindBySuffix(headers_from_libs_, abs_header);
          found.has_value()) {
        if (found->target_set->contains(target)) continue;  // found self

        // Only complain if actionable
        if (!source_content->is_generated && session_.MinVerbosity(2)) {
          maybe_log_source_headline(src_name, source_content->path, target);
          Loc(source, inc_file)
            << " fuzzy matched " << Magenta(session_) << inc_file
            << Norm(session_) << " header relative to this file. "
            << "Consider FQN relative to project root.\n";
        }
        if (session_.MinVerbosity(3)) {
          maybe_log_source_headline(src_name, source_content->path, target);
        }
        maybe_log(source, inc_file, is_bracket_include, *found->target_set);
        AddVisibleAlternativesWithStratum(target, *found->target_set, result);
        continue;
      }

      // Possible refactor-name MaybeIgnoreUnnacountedHeaderIfLooksLikeSystem()
      // Hack: seen in swig-generated files: they include "assert.h", but
      // clearly mean the system header.
      // So after we've checked all other possible providers, let's just waive
      // this one here.
      if (inc_file == "assert.h") {
        if (session_.MinVerbosity(2)) {  // quasi-benign. Only on high verbose
          maybe_log_source_headline(src_name, source_content->path, target);
          LogUnknownProvider(source, inc_file, target, "#include",
                             " (assuming system header and moving on.)", false);
        }
        continue;
      }

      // If this is a bracket include, then this is some sort of system header,
      // and it does not need to be accounted for.
      // However, we already went through all of these and matched potential
      // providers in case someone uses a vendored library but accidentally
      // used bracket includes; so we won't remove these libs (common culprit:
      // <zlib.h>)
      if (is_bracket_include) {
        continue;
      }

      // Everything beyond here, we don't really know where a header is
      // coming from, so need to be careful suggesting removal of any dep.
      *all_headers_accounted_for = false;

      if (session_.MinVerbosity(1)) {
        // Not found anywhere, but maybe we can at least report possible source.
        if (const auto found = files_from_genrules_.find(inc_file);
            found != files_from_genrules_.end()) {
          maybe_log_source_headline(src_name, source_content->path, target);
          Loc(source, inc_file)
            << " " << Magenta(session_) << inc_file << Norm(session_)
            << " not accounted for; generated by genrule "
            << found->second.ToString() << ", but not "
            << "in hdrs=[...] of any cc_library() we depend on.\n";
          continue;
        }
      }

      // More possible checks
      //  - is this part of any library, but only in the srcs=[], but not
      //    in hdrs ? Then suggest to export it in that library.
      //  - is it not mentioned anywhere, but it shows up in the filesystem ?
      //    maybe forgot to add to any library.

      // No luck. Source includes it, but we don't know where it is.
      // Be careful with remove suggestion, so consider 'not accounted for'.
      maybe_log_source_headline(src_name, source_content->path, target);
      LogUnknownProvider(source, inc_file, target, "#include",
                         " -- Missing or from non-standard bazel-rule ?");
    }
  }

  source_grep_stats.AddBytesProcessed(total_size);
  return result;
}

std::vector<absl::btree_set<BazelTarget>>
DWYUGenerator::DependenciesNeededByProtoSources(
  const BazelTarget &target, const ParsedBuildFile &build_file,
  const std::vector<std::string_view> &sources,
  bool *all_imports_accounted_for) {
  Stat &source_read_stats =
    session_.GetStatsFor("  - read(proto source)", "sources");
  Stat &source_grep_stats =
    session_.GetStatsFor("  - Grep'ed for import", "sources");

  size_t total_size = 0;

  // We log the source on -vv, but sometimes also in less verbose
  // situations. Only once per source.
  bool source_logged_already = false;
  auto maybe_log_source_headline = [&](std::string_view src_name,
                                       const std::string &path,
                                       const BazelTarget &target) {
    if (source_logged_already) return;
    Loc(project_, src_name)
      << Invert(session_) << "[ " << Bold(session_) << path << BoldOff(session_)
      << " proto import dependency check " << Bold(session_) << "(" << target
      << ") ]" << Norm(session_) << "\n";
    source_logged_already = true;
  };

  std::vector<absl::btree_set<BazelTarget>> result;

  for (const std::string_view src_name : sources) {
    source_logged_already = false;
    auto source_content =
      ReadSourceForDWYU(src_name, target, build_file, source_read_stats,
                        all_imports_accounted_for);
    if (!source_content.has_value()) continue;

    ++source_grep_stats.count;
    total_size += source_content->content.size();
    NamedLineIndexedContent source(source_content->path,
                                   source_content->content);
    std::vector<std::string_view> imports;
    {
      const ScopedTimer timer(&source_grep_stats.duration);
      imports = ExtractProtoImports(&source);
    }

    for (const std::string_view imp_file : imports) {
      if (IsHeaderInList(imp_file, sources, target.package.path)) {
        continue;
      }

      if (const auto &found = FindBySuffix(protos_from_libs_, imp_file);
          found.has_value()) {
        if (session_.MinVerbosity(3)) {
          Loc(source, imp_file) << " import \"" << imp_file << "\"\n";
          for (const BazelTarget &p : *found->target_set) {
            Loc(source, imp_file) << "    | " << p << "\n";
          }
        }
        AddVisibleAlternatives(target, *found->target_set, result);
        continue;
      }

      *all_imports_accounted_for = false;

      if (session_.MinVerbosity(1)) {
        maybe_log_source_headline(src_name, source_content->path, target);
        LogUnknownProvider(source, imp_file, target, "import",
                           " -- Missing or from non-standard bazel-rule ?");
      }
    }
  }

  source_grep_stats.AddBytesProcessed(total_size);
  return result;
}

void DWYUGenerator::CreateEditsForTarget(const BazelTarget &target,
                                         const query::Result &details,
                                         const ParsedBuildFile &build_file) {
  if (details.bant_skip_dependency_check) return;

  // Looking at the include/import files the sources reference, map these back
  // to the dependencies that provide them: these are the deps we
  // needed.
  bool all_header_deps_known = true;

  const bool is_proto_library = (details.rule == "proto_library");

  // Collect sources and headers provided by this library.
  auto sources = query::ExtractStringList(details.srcs_list);
  if (!is_proto_library) {
    query::AppendStringList(details.hdrs_list, sources);
  }

  // TODO: the following is expensive: we don't know what changed, so we have
  // to re-run the expansion over all sources again. Ideally, this should be
  // handled by ExpandFilegroupsInList() whenever it expands files.
  int max_rounds = 2;
  while (ExpandFilegroupsInList(target.package, filegroups_, &sources) &&
         --max_rounds > 0) {
  }

  // all implicit -I with includes = ["include"] elements.
  auto includes_list = query::ExtractStringList(details.includes_list);

  // Grep for all includes/imports they use to determine which deps we need
  auto deps_needed =
    is_proto_library
      ? DependenciesNeededByProtoSources(target, build_file, sources,
                                         &all_header_deps_known)
      : DependenciesNeededBySources(target, build_file, sources, includes_list,
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
      Loc(project_, dependency_target)
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
        Loc(project_, dependency_target)
          << " in target " << target << ": dependency " << dependency_target
          << " same dependency mentioned multiple times. Run buildifier\n";
      } else if (session_.MinVerbosity(1)) {
        Loc(project_, dependency_target)
          << " in target " << target << ": dependency " << Bold(session_)
          << dependency_target << Norm(session_)
          << " provides headers already provided by " << Bold(session_)
          << previously << Norm(session_)
          << " before. Multiple libraries providing the same headers ?\n";
      }
      continue;
    }

    // -- Looks like we don't need this dependency. Consider removing.

    // ... but be cautious. We can only remove if we otherwise could associate
    // a dependency for every header-include we saw.
    const bool potential_remove_suggestion_safe = all_header_deps_known;

    // There are also other reasons why we might not want to remove a dependency
    bool veto_removal = IsAlwayslink(*requested_target);
    if (!veto_removal) {  // But maybe buildcleaner:keep ?
      static const LazyRE2 kExcludeVetoUserCommentRe{"#.*keep"};
      const auto line = project_.GetSurroundingLine(dependency_target);
      veto_removal = (!session_.flags().ignore_keep_comment &&
                      RE2::PartialMatch(line, *kExcludeVetoUserCommentRe));
    }

    // Emit the edits.
    if (!veto_removal) {
      if (potential_remove_suggestion_safe) {
        emit_deps_edit_(EditRequest::kRemove, target, dependency_target, "");
      } else if (session_.MinVerbosity(2)) {
        Loc(project_, dependency_target)
          << " " << Bold(session_) << requested_target->ToString()
          << Norm(session_) << " dependency looks superfluous in "
          << Bold(session_) << target << Norm(session_)
          << ", but there are also unaccounted sources. Won't remove.\n";
      }
    }
  }

  // Now, if there is still something we need, add them.
  for (const auto &need_add_alternatives : deps_needed) {
    // Only possible to auto-add if there is exactly one alternative.
    if (need_add_alternatives.size() > 1) {
      Loc(project_, details.name)
        << " Can't auto-fix: Referenced headers in " << target
        << " need exactly one of multiple choices\nAlternatives are:\n";
      for (const BazelTarget &target : need_add_alternatives) {
        session_.info() << "\t" << target << "\n";
      }
      continue;
    }

    const BazelTarget &need_add = *need_add_alternatives.begin();
    if (need_add == target) {  // That's us. Not needed (but why not caught?)
      continue;
    }
    std::string visibility_msg;
    if (CanSee(target, need_add, &visibility_msg) &&
        IsTestonlyCompatible(target, need_add)) {
      emit_deps_edit_(EditRequest::kAdd, target, "",
                      need_add.ToStringRelativeTo(target.package));
      if (session_.MinVerbosity(1)) {
        if (auto reason = DeprecationReason(need_add)) {
          Loc(project_, details.name)
            << " Only suitable dependency " << need_add
            << " is deprecated: " << *reason << "\n";
        }
      }
    } else if (session_.MinVerbosity(2)) {
      Loc(project_, details.name)
        << ": Would add " << need_add << ", but not visible. " << visibility_msg
        << "\n";
    }
  }
}

std::ostream &DWYUGenerator::Loc(const SourceLocator &locator,
                                 std::string_view where) const {
  std::ostream &out = session_.info();
  out << BlueBold(session_);
  const FileLocation loc = locator.GetLocation(where);
  out << HyperLinked{session_.linkgen(), loc};
  out << Norm(session_);
  return out;
}

// -- Publically visible interface

DWYUGenerator::DWYUGenerator(Session &session, const ParsedProject &project,
                             EditCallback emit_deps_edit)
    : session_(session),
      project_(project),
      emit_deps_edit_(std::move(emit_deps_edit)) {
  Stat &stats = session_.GetStatsFor("  - DWYU preparation", "indexed targets");
  const ScopedTimer timer(&stats.duration);

  // TODO: we create this filegroups multiple times: here, but then the
  // ExtractExpandedHeaderToLibMapping() also internally does the same thing.
  // We should just pass this through.
  filegroups_ = ExtractFilegroupTargets(project);

  headers_from_libs_ =
    ExtractExpandedHeaderToLibMapping(project, session.info(),
                                      /*suffix_index=*/true);
  protos_from_libs_ = ExtractProtoToProtoLibMapping(project, session.info(),
                                                    /*suffix_index=*/true);
  files_from_genrules_ = ExtractGeneratedFromGenrule(project, session.info());
  InitKnownLibraries();
  stats.count = known_libs_.size();
}

size_t DWYUGenerator::CreateEditsForPattern(const BazelTargetMatcher &pattern) {
  size_t matching_patterns = 0;
  for (const auto &[_, parsed_package] : project_.ParsedFiles()) {
    const BazelPackage &current_package = parsed_package->package;
    if (!pattern.Match(current_package)) {
      continue;
    }
    query::FindTargets(
      parsed_package->ast,
      {"cc_library", "cc_binary", "cc_test", "proto_library"},
      [&](const query::Result &details) {
        auto target = current_package.QualifiedTarget(details.name);
        if (!target.has_value() || !pattern.Match(*target)) {
          return;
        }
        ++matching_patterns;
        CreateEditsForTarget(*target, details, *parsed_package);
      });
  }
  return matching_patterns;
}

void DWYUGenerator::PrintGenruleTargetsToRun(std::ostream &out) {
  if (suggested_genrules_to_run_.empty()) return;
  out << "\n"
      << Bold(session_)
      << "[ Run the following rules for bant dwyu to see generated files. ]"
      << Norm(session_) << "\n";
  out << "bazel build --remote_download_outputs=all ";
  out << absl::StrJoin(suggested_genrules_to_run_, " ");
  out << "\n";
}

void DWYUGenerator::PrintSourcesNotFound(std::ostream &out) {
  if (header_without_provider_.empty()) return;
  out << "\n"
      << Bold(session_)
      << "[ Summary of includes that were seen in sources but no known "
      << "libraries providing them. ]" << Norm(session_) << "\n\n";
  out << Bold(session_)
      << "Debugging tip to narrow (Also note, some might be benign, e.g. "
         "behind #ifdefs)"
      << Norm(session_);
  out << "\n\n\t$ bant print -m ... -g <include>  # prints any rule mentioning "
         "header\n\n";
  for (const auto &[header, srcs] : header_without_provider_) {
    out << Magenta(session_) << header << Norm(session_);
    if (header.find_first_of('/') == std::string::npos) {
      out << Red(session_) << " (file without path will not be fuzzy-matched)"
          << Norm(session_);
    }
    out << "\n";
    for (const std::string &src_loc : srcs) {
      out << "\t" << src_loc << "\n";
    }
  }
  out << "\n";
}

size_t CreateDependencyEdits(Session &session, const ParsedProject &project,
                             const BazelTargetMatcher &pattern,
                             const EditCallback &emit_deps_edit) {
  Stat &dwyu_stats = session.GetStatsFor("DWYU Operation", "targets");
  const ScopedTimer timer(&dwyu_stats.duration);

  size_t edits_emitted = 0;
  const EditCallback edit_counting_forwarder =
    [&](EditRequest op, const BazelTarget &target,  //
        std::string_view before, std::string_view after) {
      ++edits_emitted;
      emit_deps_edit(op, target, before, after);
    };
  DWYUGenerator gen(session, project, edit_counting_forwarder);
  const size_t target_count = gen.CreateEditsForPattern(pattern);
  dwyu_stats.count += target_count;
  session.info() << "Checked DWYU on " << target_count << " targets.";
  if (target_count == 0 && pattern.HasFilter()) {
    session.info()
      << "\nNote: No cc_library/cc_binary/cc_test/proto_library targets"
         " matched the pattern. Target might not exist or uses an unknown"
         " custom rule."
         "\nConsider adding a macro to your project's .bant-macros file.";
  }
  if (edits_emitted) {
    session.info() << " Emitted " << edits_emitted << " edits.";
  }
  session.info() << "\n";
  gen.PrintGenruleTargetsToRun(session.info());
  if (session.MinVerbosity(2)) {
    gen.PrintSourcesNotFound(session.info());
  }
  return edits_emitted;
}
}  // namespace bant
