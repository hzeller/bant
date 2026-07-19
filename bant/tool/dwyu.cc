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
#include "bant/explore/project-indexing.h"
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
// A lot of 'noise' comes from the extensive logging for debug reasons.

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
  // Don't do relative includes like that.
  while (header.starts_with("../")) {
    header.remove_prefix(3);
    if (auto last_slash = prefix_path.find_last_of('/');
        last_slash != std::string_view::npos) {
      prefix_path = prefix_path.substr(0, last_slash);
    }
  }

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

static AlternativeSet intersect(const AlternativeSet &a,
                                const AlternativeSet &b) {
  AlternativeSet result;
  std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                        std::inserter(result, result.begin()));
  return result;
}

std::ostream &DWYUGenerator::LogDepSet(const char *msg,
                                       const IncludeNeededDepsAlternatives &d) {
  std::ostream &out = session_.info();
  // This is in super-high verbosity, so printing annoying inverted headers
  // helps finding this in the myriad of other messages.
  out << Invert(session_) << msg << Norm(session_) << " [";
  for (const auto &altenatives : d) {
    if (altenatives.empty()) continue;
    if (altenatives.size() == 1) out << Dim(session_);  // More boring
    out << "{";
    bool is_first = true;
    for (const auto &t : altenatives) {
      if (!is_first) out << ", ";
      out << t;
      is_first = false;
    }
    out << "}, ";
    if (altenatives.size() == 1) out << Norm(session_);
  }
  out << "] ; " << Bold(session_) << "(" << d.size() << " sets)"
      << Norm(session_);
  return out;
}

// Input is a list of dependency alternative we need: for each header file,
// there are potentially multiple libraries that are providing these,
// the 'alternatives'. So we have a bag of alternative sets.
// Output is a potentially smaller set of smaller alternatives.
static IncludeNeededDepsAlternatives MinimizeDependencySet(
  const IncludeNeededDepsAlternatives &to_reduce) {
  // Find all the sets that intersect, and only remember the intersection.
  // The intersection will be sufficient to satisfy the dependency requirements
  // for both.

  // n^2, but usually pretty small n.
  IncludeNeededDepsAlternatives result;
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

// Test if tags list contains a tag with given context; if so, return its
// value (locatable string);
static std::optional<std::string_view> TagContains(List *tags_list,
                                                   std::string_view query_tag) {
  if (!tags_list) return std::nullopt;
  const auto tags = query::ExtractStringList(tags_list);
  if (auto found = std::find(tags.begin(), tags.end(), query_tag);
      found != tags.end()) {
    return *found;
  }
  return std::nullopt;
}

// Various predicates to check
bool DWYUGenerator::DependencySaysShouldKeep(const BazelTarget &target,
                                             ShouldKeepMessage *msg) const {
  auto found = known_libs_.find(target);
  if (found == known_libs_.end()) return true;  // Unknown ? Be conservative.
  const query::Result &dep = found->second;
  // TODO: follow all libs we depend on ?
  if (dep.alwayslink_scalar && dep.alwayslink_scalar->AsInt()) {
    *msg = {"alwayslink", dep.alwayslink_scalar->AsString()};
    return true;
  }
  if (auto keep_dep = TagContains(dep.tags, "keep_dep"); keep_dep.has_value()) {
    *msg = {"tag", *keep_dep};
    return true;
  }
  if (dep.rule == "cc_library" && (!dep.hdrs_list || dep.hdrs_list->empty())) {
    *msg = {"empty hdrs = []", dep.name};
    return true;
  }
  return false;
}

bool DWYUGenerator::CommentSaysShouldKeepDependency(
  std::string_view dep_in_file, ShouldKeepMessage *msg) const {
  static const LazyRE2 kExcludeVetoUserCommentRe{"(#.*keep.*)"};
  const auto line = project_.GetSurroundingLine(dep_in_file);
  std::string_view keep_match;
  if ((!session_.flags().ignore_keep_comment &&
       RE2::PartialMatch(line, *kExcludeVetoUserCommentRe, &keep_match))) {
    *msg = {"comment", keep_match};
    return true;
  }
  return false;
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

std::optional<std::string_view> DWYUGenerator::AvoidDueToVisibility(
  const BazelTarget &target, const BazelTarget &dep) const {
  const auto found = known_libs_.find(dep);
  if (found == known_libs_.end()) return std::nullopt;  // Unknown ? Be Bold.

  if (target.package == dep.package) {
    // We can implicitly see all the targets in the same package.
    return std::nullopt;
  }

  // Somewhat ugly hack: the protobuf library has a protobuf_headers library
  // that does not acctually provide any actual libraries. From the comment
  // there it is there for some shared object building rules; but we should
  // not depend on it, so pretend we can't see it.
  if (found->second.name == "protobuf_headers") {
    // "protobuf_headers don't actually provide implementation";
    return found->second.name;
  }

  List *visibility_list = found->second.visibility;
  if (!visibility_list) return std::nullopt;
  bool any_valid_visiblity_pattern = false;
  for (const std::string_view vis : query::ExtractStringList(visibility_list)) {
    if (vis.empty()) continue;
    std::vector<std::string_view> patterns_to_consider;

    // Maybe a package group ?
    if (auto maybe_group_target = BazelTarget::ParseFrom(vis, dep.package);
        maybe_group_target.has_value()) {
      patterns_to_consider =
        ResolvePackageGroupPatterns(packagegroups_, *maybe_group_target);
    }
    if (patterns_to_consider.empty()) {
      patterns_to_consider.emplace_back(vis);  // let's assume it is pattern
    }

    BazelPatternBundle vis_bundle;
    for (const std::string_view vis_pattern : patterns_to_consider) {
      auto vis_or = BazelPattern::ParseVisibility(vis_pattern, dep.package);
      if (!vis_or.has_value()) continue;
      any_valid_visiblity_pattern = true;
      vis_bundle.AddPattern(*vis_or);
    }
    vis_bundle.Finish();

    if (vis_bundle.Match(target)) {
      return std::nullopt;
    }
  }

  // There might be variables and other things that we couldn't elaborate.
  // So in case there was not a any pattern we can expand, assume this to
  // be public visibility.
  if (!any_valid_visiblity_pattern) return std::nullopt;

  // The visibility label is the closest locatable.
  return found->second.visibility_label;
}

std::optional<std::string_view> DWYUGenerator::AvoidDependencyReason(
  const BazelTarget &self, const BazelTarget &dep) const {
  if (auto found = known_libs_.find(dep); found != known_libs_.end()) {
    if (!found->second.deprecation.empty()) {
      return found->second.deprecation;
    }
    if (auto avoid_dep = TagContains(found->second.tags, "avoid_dep");
        avoid_dep.has_value()) {
      return avoid_dep;  // Original string_view from file.
    }

    // Hack: the gtest_for_library should be avoided as well, but it doesn't
    // have a tag. Let's manually point to the fact that it is an alias
    // (as we nee to have a locatable string).
    if (found->second.name == "gtest_for_library") {
      return found->second.rule;
    }
  }
  return AvoidDueToVisibility(self, dep);
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
  Loc(source, ref_file) << " " << Bold(session_) << ref_keyword << " \""
                        << ref_file << "\" " << Norm(session_) << Red(session_)
                        << "(unknown provider)" << Norm(session_) << "\n";
  Loc(source, ref_file) << "    ?      ^ " << extra_info << "\n";
}

// TODO; the whole alternativs (right now: just a target) should probably
// something like a RankedAlternative (tuple target, rank) in which we express
// preferred alternatives, in which we then can fold avoid, deprecation,
// stratum, visibility.
// Also possibly a boost if we find that a particular dependency looks like if
// it is actually the implementing one (say two targets that both claim to
// export header foo.h but one depends on the other (strong hint) or one has a
// corresponding foo.cc (weaker hint)

void DWYUGenerator::AddVisibleAlternatives(
  const BazelTarget &target, const AlternativeSet &alternatives,
  IncludeNeededDepsAlternatives &result) {
  AlternativeSet no_limits;
  AlternativeSet avoid_if_possible;
  for (const BazelTarget &t : alternatives) {
    if (AvoidDependencyReason(target, t).has_value()) {
      avoid_if_possible.emplace(t);
    } else {
      no_limits.emplace(t);
    }
  }
  if (!no_limits.empty()) {
    result.push_back(std::move(no_limits));
  } else if (!avoid_if_possible.empty()) {
    // If we _only_ have deprecated alternatives, consider them visible.
    result.push_back(std::move(avoid_if_possible));
  }
}

// Add alternatives, but rank root > bazel_dep() > randomly found dependencies
void DWYUGenerator::AddVisibleAlternativesWithStratum(
  const BazelTarget &target, const AlternativeSet &alternatives,
  IncludeNeededDepsAlternatives &result) {
  Range stratum_range;
  std::vector<BazelTarget> temp_result;
  bool found_non_avoiding = false;
  for (const BazelTarget &t : alternatives) {
    const bool is_to_avoid = AvoidDependencyReason(target, t).has_value();
    if (is_to_avoid && found_non_avoiding) continue;
    if (!is_to_avoid && !found_non_avoiding) {
      // Until we find the first non-avoid alternative, we also
      // keep to avoid targets as they might be our only chance.
      temp_result.clear();
      stratum_range = Range{};
      found_non_avoiding = true;
    }
    const int stratum = GetStratum(t);
    stratum_range.Update(stratum);
    if (stratum <= stratum_range.min_value) {
      temp_result.emplace_back(t);
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

static bool AnyAlternativeInProvidedDeps(
  const AlternativeSet &alternatives,
  const OneToOne<BazelTarget, std::string_view> &declared_deps) {
  auto map_it = declared_deps.begin();
  auto set_it = alternatives.begin();

  while (map_it != declared_deps.end() && set_it != alternatives.end()) {
    if (map_it->first < *set_it) {
      // The map key is too small; skip forward.
      // Optimization: use lower_bound to leapfrog if there's a huge gap
      map_it = declared_deps.lower_bound(*set_it);
    } else if (*set_it < map_it->first) {
      // The set key is too small; skip forward.
      set_it = alternatives.lower_bound(map_it->first);
    } else {
      return true;
    }
  }
  return false;
}

// TODO: the following does a bunch per source file. This should probably
// be encasulated in a struct or class PerSourceFileDWYU that captures
// all the context and has methods such as HeaderMentionedInOwnSources()
// HeaderIsMentionedInOwnSourceWithIncludePath() etc.
// That way we avoid the various unnamed blocks in a huge loop.
IncludeNeededDepsAlternatives DWYUGenerator::DependenciesNeededBySources(
  const BazelTarget &target, const ParsedBuildFile &build_file,
  const std::vector<std::string_view> &sources,            // srcs, hdrs
  const std::vector<std::string_view> &includes_dir_list,  // includes = []
  const DefineMap &defines, const TargetToFileLocation &declared_deps,
  absl::btree_set<BazelTarget> *conservatively_no_remove,
  bool *all_headers_accounted_for) {
  Stat &source_read_stats =
    session_.GetStatsFor("  - read(C++ source)", "sources");
  Stat &source_grep_stats =
    session_.GetStatsFor("  - C++ preprocess; extract #inc", "sources");

  const bool bracket_inc_is_ignore =
    session_.flags().dwyu_bracket_include == BracketIncHandling::kIgnore;

  const bool bracket_inc_is_validate =
    session_.flags().dwyu_bracket_include == BracketIncHandling::kValidate;

  const bool bracket_inc_is_acknowlege =
    session_.flags().dwyu_bracket_include == BracketIncHandling::kAcknowledge;

  size_t total_size = 0;

  // In verbosity 3, we always show alternatives, in verbosity 2 we
  // we only show headers with missing libraries.
  auto should_log_alternatives_p =
    [&](const AlternativeSet &alternatives) -> bool {
    if (session_.MinVerbosity(3)) return true;
    if (!session_.MinVerbosity(2)) return false;
    return !AnyAlternativeInProvidedDeps(alternatives, declared_deps);
  };

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

  auto log_angle_bracket_codesmell = [&](const NamedLineIndexedContent &source,
                                         const TaggedInclude &inc) {
    // Nasty code-smell thus always show early in verbosity.
    if (!bracket_inc_is_validate || !session_.MinVerbosity(1)) return;
    if (!inc.is_angle_bracket) return;
    const std::string see_also =
      !session_.MinVerbosity(3) ? " See with -vvv" : "";
    Loc(source, inc.include)
      << " source of " << Bold(session_) << target << Norm(session_)
      << ": #include " << Magenta(session_) << "<" << inc.include << ">"
      << Norm(session_) << " uses <>-bracketed include style. " << Red(session_)
      << "Should use quote-style "
      << "\"" << inc.include << "\" as this header is provided by "
      << "project libraries." << see_also << Norm(session_) << "\n";
  };

  // Log providers if super verbose -vvv
  // This shows a after the include all the dependencies that can provide
  // it.
  auto log_alternatives_for_include = [&](const NamedLineIndexedContent &source,
                                          const TaggedInclude &inc,
                                          const AlternativeSet &alternatives) {
    // Show #include and possibly preprocessing #ifdef condition we're in.
    Loc(source, inc.include)
      << Bold(session_) << " #include " << (inc.is_angle_bracket ? '<' : '"')
      << inc.include << (inc.is_angle_bracket ? '>' : '"') << Norm(session_);
    if (!inc.active_preprocessing_condition.empty() &&
        !inc.likely_header_guard_condition) {
      if (inc.is_ifdefed_out) {  // if we allow all branches, this might show
        session_.info() << Red(session_) << " (PP: in ";
      } else {
        session_.info() << Green(session_) << " (PP: in ";
      }
      if (!inc.else_location.empty()) {
        const FileLocation loc = source.GetLocation(inc.else_location);
        session_.info() << Bold(session_)
                        << HyperLinked(session_.linkgen(), loc,
                                       inc.else_location)
                        << BoldOff(session_) << " branch of ";
      }
      const FileLocation loc =
        source.GetLocation(inc.active_preprocessing_condition);
      session_.info() << "condition " << Bold(session_)
                      << HyperLinked(session_.linkgen(), loc,
                                     inc.active_preprocessing_condition)
                      << BoldOff(session_);
      session_.info() << ")" << Norm(session_);
    }
    session_.info() << "\n";

    for (const BazelTarget &possible_provider : alternatives) {
      std::stringstream msg;
      if (auto reason = AvoidDependencyReason(target, possible_provider);
          reason.has_value()) {
        const FileLocation loc = project_.GetLocation(*reason);
        msg << Red(session_) << " (avoid if possible: "
            << HyperLinked(session_.linkgen(), loc, *reason) << ")"
            << Norm(session_);
      }
      const auto found_declared = declared_deps.find(possible_provider);
      if (found_declared != declared_deps.end()) {
        // If the dependency is already declared in target deps=[], add
        // a checkmark and hyperlink it to the location in the BUILD file.
        const std::string anchor_text = absl::StrCat("✓ ", possible_provider);
        const FileLocation loc = project_.GetLocation(found_declared->second);
        Loc(source, inc.include)
          << "    " << HyperLinked{session_.linkgen(), loc, anchor_text}
          << msg.str() << "\n";
      } else {
        // ... otherwise just print the would-be provider.
        Loc(source, inc.include)
          << "    - " << possible_provider << msg.str() << "\n";
      }
    }
  };

  bool any_maybe_not_intended_ifdef_out = false;
  IncludeNeededDepsAlternatives result;
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
    std::vector<TaggedInclude> pound_includes;
    {
      const ScopedTimer timer(&source_grep_stats.duration);
      pound_includes = ExtractCCIncludes(&source, defines);
    }
    // Now for all includes, we need to make sure we can account for it.
    for (const TaggedInclude inc : pound_includes) {
      if (inc.is_angle_bracket && bracket_inc_is_ignore) continue;
      const std::string_view inc_file = inc.include;

      if (inc.is_ifdefed_out) {
        // TODO: should this look at bracket inc or rather some ifdef flag ?
        if (session_.MinVerbosity(1) && bracket_inc_is_validate) {
          // TODO: the following should really only be reported if this results
          // in a removal, otherwise a higher verbosity level might be ok.
          maybe_log_source_headline(src_name, source_content->path, target);
          const auto active_condition = inc.active_preprocessing_condition;
          const FileLocation loc = source.GetLocation(active_condition);
          Loc(source, inc_file)
            << " " << Bold(session_) << inc_file << Norm(session_)
            << " not considered due to macro "
            << HyperLinked(session_.linkgen(), loc, active_condition)
            << Red(session_)
            << "if not intended, make sure to set "
               "defines=[] or copts=[] in "
            << Norm(session_) << Bold(session_) << target << Norm(session_)
            << "\n";
        }
        any_maybe_not_intended_ifdef_out = true;
        continue;
      }

      // Possible refactor-name HeaderMentionedInOwnSources()
      if (IsHeaderInList(inc_file, sources, target.package.path)) {
        continue;  // Cool, our own list srcs=[...], hdrs=[...]
      }

      // Possible refactor-name HeaderIsMentionedInOwnSourceWithIncludePath()
      // Check for all include prefices found in includes = [], effectively
      // making includes visible under shorter paths.
      bool found_local_inc = false;
      for (const std::string_view src_prefix : includes_dir_list) {
        if (IsHeaderInList(inc_file, sources, src_prefix)) {
          // Only complain if actionable
          if (!source_content->is_generated && session_.MinVerbosity(3)) {
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
        if (!source_content->is_generated && session_.MinVerbosity(3)) {
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
        log_angle_bracket_codesmell(source, inc);
        if (should_log_alternatives_p(*found_result.target_set)) {
          maybe_log_source_headline(src_name, source_content->path, target);
          log_alternatives_for_include(source, inc, *found_result.target_set);
        }
        if (inc.is_angle_bracket && bracket_inc_is_acknowlege) {
          // only prevent removal: make sure it is not removed.
          conservatively_no_remove->insert(header_providers.begin(),
                                           header_providers.end());
        } else {
          AddVisibleAlternativesWithStratum(target, header_providers, result);
        }
        continue;
      }

      // Possible refactor-name FindDependencyFromHeaderNameFuzzyDirMatch()
      // Maybe include is not provided with path relative to project root ?
      const std::string abs_header = build_file.package.QualifiedFile(inc_file);
      if (const auto &found = FindBySuffix(headers_from_libs_, abs_header);
          found.has_value()) {
        if (found->target_set->contains(target)) continue;  // found self

        // Only complain if actionable
        if (!source_content->is_generated && session_.MinVerbosity(3)) {
          maybe_log_source_headline(src_name, source_content->path, target);
          Loc(source, inc_file)
            << " fuzzy matched " << Magenta(session_) << inc_file
            << Norm(session_) << " header relative to this file. "
            << "Consider FQN relative to project root.\n";
        }
        log_angle_bracket_codesmell(source, inc);
        if (should_log_alternatives_p(*found->target_set)) {
          maybe_log_source_headline(src_name, source_content->path, target);
          log_alternatives_for_include(source, inc, *found->target_set);
        }
        if (inc.is_angle_bracket && bracket_inc_is_acknowlege) {
          // only prevent removal: make sure it is not removed.
          conservatively_no_remove->insert(found->target_set->begin(),
                                           found->target_set->end());
        } else {
          AddVisibleAlternativesWithStratum(target, *found->target_set, result);
        }
        continue;
      }

      // Possible refactor-name MaybeIgnoreUnnacountedHeaderIfLooksLikeSystem()
      // Hack: seen in swig-generated files: they include "assert.h", but
      // clearly mean the system header.
      // So after we've checked all other possible providers, let's just waive
      // this one here.
      if (inc_file == "assert.h") {
        // quasi-benign. Only on high verbose, and only if actionable.
        if (!source_content->is_generated && session_.MinVerbosity(2)) {
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
      if (inc.is_angle_bracket) {
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

  if (any_maybe_not_intended_ifdef_out && session_.MinVerbosity(3)) {
    for (const auto &[key, _] : defines) {
      // TODO: should we only emit the ones that were actually used by the
      // preprocessor ? Then we can probably do lower verbosity.
      Loc(project_, key) << " FYI, macro " << Magenta(session_)
                         << Bold(session_) << key << Norm(session_)
                         << " definition visible in " << Bold(session_)
                         << target << Norm(session_) << "\n";
    }
  }

  source_grep_stats.AddBytesProcessed(total_size);
  return result;
}

IncludeNeededDepsAlternatives DWYUGenerator::DependenciesNeededByProtoSources(
  const BazelTarget &target, const ParsedBuildFile &build_file,
  const std::vector<std::string_view> &sources,
  bool *all_imports_accounted_for) {
  Stat &source_read_stats =
    session_.GetStatsFor("  - read(proto source)", "sources");
  Stat &source_grep_stats =
    session_.GetStatsFor("  - Proto extract import", "sources");

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

  IncludeNeededDepsAlternatives result;

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
  if (details.bant_skip_dependency_check ||
      TagContains(details.tags, "nofixdeps").has_value()) {
    return;
  }

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

  // We always want to see the local defines for _our_ target.
  DefineMap defines = GetDefinesFromTarget(details, true);

  // All the dependencies; BazelTarget -> locatable string view in file.
  // Also, we update the defines with all direct dependency defines while
  // at it. This is somewhat of a double duty, returning a value but also
  // modifying `defines`, and as such somewhat ugly. But we save doing the
  // same ParseFrom() etc. twice. Maybe break up and make it slow if ugliness
  // is too much...
  const TargetToFileLocation all_declared_dependencies = [&]() {
    TargetToFileLocation result;
    const auto deps =
      query::ExtractStringList({details.deps_list, details.impl_deps_list});
    for (const std::string_view dependency_target : deps) {
      const auto requested_target =
        BazelTarget::ParseFrom(dependency_target, target.package);
      if (!requested_target.has_value()) {
        Loc(project_, dependency_target)
          << " Invalid target name '" << dependency_target << "'\n";
        continue;
      }

      if (!result.emplace(*requested_target, dependency_target).second) {
        Loc(project_, dependency_target)
          << " in target " << target << ": dependency " << dependency_target
          << " same dependency mentioned multiple times. Run buildifier\n";
      }

      // Let's see if there are defines.
      if (const auto found = defines_for_targets_.find(*requested_target);
          found != defines_for_targets_.end()) {
        defines.insert(found->second.begin(), found->second.end());
      }
    }
    return result;
  }();

  absl::btree_set<BazelTarget> conservatively_no_remove;
  // Grep for all includes/imports they use to determine which deps we need
  auto deps_needed = is_proto_library
                       ? DependenciesNeededByProtoSources(
                           target, build_file, sources, &all_header_deps_known)
                       : DependenciesNeededBySources(
                           target, build_file, sources, includes_list, defines,
                           all_declared_dependencies, &conservatively_no_remove,
                           &all_header_deps_known);

  if (session_.MinVerbosity(4)) {
    LogDepSet("Raw needed dependencies: ", deps_needed) << "\n";
  }
  deps_needed = MinimizeDependencySet(deps_needed);
  if (session_.MinVerbosity(4)) {
    LogDepSet("After minimizing: ", deps_needed) << "\n";
  }

  OneToOne<BazelTarget, BazelTarget> checked_off_by;
  auto IsNeededInSourcesAndCheckOff = [&](const BazelTarget &target) -> bool {
    for (auto it = deps_needed.begin(); it != deps_needed.end(); ++it) {
      if (it->contains(target)) {
        for (const BazelTarget &check : *it) {
          checked_off_by.insert({check, target});  // remember who checked off.
        }
        deps_needed.erase(it);  // alternatives satisifed. Remove.
        // TODO: we should keep going and find all alternatives that might
        // also match (or: verify that we don't need that).
        return true;
      }
    }
    return false;
  };

  // Check all the dependencies that the build target requested and strike
  // them off the 'deps_needed' list.
  // Everything deps_needed
  // verify we actually need them. If not: remove.
  for (const auto &[requested_target, dep_in_file] :
       all_declared_dependencies) {
    // Strike off the dependency requested in the build file from the
    // dependendencies we independently determined from the #includes.
    // If it is not on that list, it is a canidate for removal.
    if (IsNeededInSourcesAndCheckOff(requested_target)) {
      continue;
    }

    if (checked_off_by.contains(requested_target)) {
      const BazelTarget &previously = checked_off_by[requested_target];
      if (session_.MinVerbosity(1)) {
        Loc(project_, dep_in_file)
          << " in target " << target << ": dependency " << Bold(session_)
          << dep_in_file << Norm(session_)
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

    // There might be reasons why we might not want to remove a dependency
    ShouldKeepMessage keep_msg;
    const bool veto_removal =
      DependencySaysShouldKeep(requested_target, &keep_msg) ||
      CommentSaysShouldKeepDependency(dep_in_file, &keep_msg) ||
      conservatively_no_remove.contains(requested_target);

    if (veto_removal && !keep_msg.locatable_reason.empty() &&
        session_.MinVerbosity(3)) {
      Loc(project_, keep_msg.locatable_reason)
        << " Keeping " << Bold(session_) << requested_target << Norm(session_)
        << " due to " << keep_msg.message << ": '" << Dim(session_)
        << keep_msg.locatable_reason << Norm(session_) << "'\n";
    }

    // Emit the edits.
    if (!veto_removal) {
      if (potential_remove_suggestion_safe) {
        emit_deps_edit_(EditRequest::kRemove, target, dep_in_file, "");
      } else if (session_.MinVerbosity(2)) {
        Loc(project_, dep_in_file)
          << " " << Bold(session_) << requested_target.ToString()
          << Norm(session_) << " dependency looks superfluous in "
          << Bold(session_) << target << Norm(session_)
          << ", but there are also unaccounted sources. Won't remove.\n";
      }
    }
  }

  if (session_.MinVerbosity(4)) {
    LogDepSet("After checking off existing deps=[]: ", deps_needed) << "\n";
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

    // We typically would add 'to avoid' targets if this is the last resort.
    // However, if this is due to visibility, we should not get us into trouble,
    // and not add; test for this situation here right before we'd add.
    auto possible_visibility_veto = AvoidDueToVisibility(target, need_add);
    if (!possible_visibility_veto.has_value() &&
        IsTestonlyCompatible(target, need_add)) {
      emit_deps_edit_(EditRequest::kAdd, target, "",
                      need_add.ToStringRelativeTo(target.package));
      if (session_.MinVerbosity(1)) {
        if (auto reason = AvoidDependencyReason(target, need_add)) {
          Loc(project_, details.name)
            << " Can't avoid even though '" << *reason << "': " << need_add
            << " is the only suitable dependency\n";
        }
      }
    } else if (session_.MinVerbosity(2) &&
               possible_visibility_veto.has_value()) {
      const std::string_view viz_veto = *possible_visibility_veto;
      const FileLocation loc = project_.GetLocation(viz_veto);
      Loc(project_, details.name)
        << ": Would add " << need_add << ", but not matching "
        << HyperLinked(session_.linkgen(), loc, viz_veto) << "\n";
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

  // for visibilitychecks
  packagegroups_ = ExtractPackageGroups(project);

  headers_from_libs_ =
    ExtractExpandedHeaderToLibMapping(project, session.info(),
                                      /*suffix_index=*/true);
  protos_from_libs_ = ExtractProtoToProtoLibMapping(project, session.info(),
                                                    /*suffix_index=*/true);
  files_from_genrules_ = ExtractGeneratedFromGenrule(project, session.info());

  // The following is a utility that should probably go to project-indexing.h
  for (const auto &[_, build_file] : project.ParsedFiles()) {
    if (!build_file->ast) continue;
    query::FindTargets(
      build_file->ast, {"cc_library"}, [&](const query::Result &cc_lib) {
        if (cc_lib.defines == nullptr || cc_lib.defines->empty()) return;
        auto target = build_file->package.QualifiedTarget(cc_lib.name);
        if (!target.has_value()) return;
        defines_for_targets_[*target] = GetDefinesFromTarget(cc_lib, false);
      });
  }

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

  size_t edits_requested = 0;
  size_t post_filter_edits = 0;
  const EditCallback edit_counting_forwarder =
    [&](EditRequest op, const BazelTarget &target,  //
        std::string_view before, std::string_view after) {
      const bool is_emitted = emit_deps_edit(op, target, before, after);
      ++edits_requested;
      if (is_emitted) ++post_filter_edits;
      return is_emitted;
    };
  DWYUGenerator gen(session, project, edit_counting_forwarder);
  const size_t target_count = gen.CreateEditsForPattern(pattern);
  dwyu_stats.count += target_count;
  session.info() << Dim(session) << "Checked DWYU on " << target_count
                 << " targets." << Norm(session);
  if (target_count == 0 && pattern.HasFilter()) {
    session.info()
      << "\nNote: No cc_library/cc_binary/cc_test/proto_library targets"
         " matched the pattern. Target might not exist or uses an unknown"
         " custom rule."
         "\nConsider adding a macro to your project's .bant-macros file.";
  }
  if (edits_requested) {
    session.info() << Dim(session) << " Requested " << edits_requested
                   << " edits.";
    if (edits_requested != post_filter_edits) {
      session.info() << " Grep-selected " << post_filter_edits << " of these.";
    }
    session.info() << Norm(session);
  }
  session.info() << "\n";
  gen.PrintGenruleTargetsToRun(session.info());
  if (session.MinVerbosity(2)) {
    gen.PrintSourcesNotFound(session.info());
  }
  return edits_requested;
}
}  // namespace bant
