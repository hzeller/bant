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

#include "bant/types-bazel.h"

#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "bant/workspace.h"
#include "re2/re2.h"

namespace bant {
std::string BazelPackage::ToString() const {
  return absl::StrCat(project, "//", path);
}

std::string BazelPackage::QualifiedFile(std::string_view relative_file) const {
  if (relative_file.starts_with(':')) {
    relative_file.remove_prefix(1);
  }
  if (path.empty()) return std::string(relative_file);
  return absl::StrCat(path, "/", relative_file);
}

std::string BazelPackage::FullyQualifiedFile(
  const BazelWorkspace &workspace, std::string_view relative_file) const {
  std::string root_dir;
  if (!project.empty()) {
    auto package_path = workspace.FindPathByProject(project);
    if (package_path.has_value()) {
      root_dir = package_path->path();
    }
  }
  if (!root_dir.empty()) root_dir.append("/");
  return root_dir.append(QualifiedFile(relative_file));
}

/*static*/ std::optional<BazelPackage> BazelPackage::ParseFrom(
  std::string_view str) {
  auto maybe_colon = str.find_last_of(':');
  str = str.substr(0, maybe_colon);

  using absl::StrSplit;
  if (str.size() < 2) return std::nullopt;
  std::string_view project;
  std::string_view path;
  if (str[0] == '@') {
    project = str.substr(0, str.find_first_of('/'));
    path = str.substr(project.length());
    if (project == "@") {
      project = "";  // This is just our project package.
    }
  } else {
    project = "";
    path = str;
  }
  while (!path.empty() && path.front() == '/') path.remove_prefix(1);
  while (!path.empty() && path.back() == '/') path.remove_suffix(1);
  if (absl::StrContains(path, "//")) {
    return std::nullopt;  // Something is off.
  }
  auto tilde_pos = project.find_first_of('~');
  if (tilde_pos != std::string_view::npos) {  // bzlmod puts version after ~
    // version = project.substr(tilde_pos + 1);
    project = project.substr(0, tilde_pos);
  }
  return BazelPackage(project, path);
}

/*static*/ std::optional<BazelTarget> BazelTarget::ParseFrom(
  std::string_view str, const BazelPackage &context) {
  const std::string_view project = context.project;
  std::string_view package;
  std::string_view target;

  std::vector<std::string_view> parts = absl::StrSplit(str, ':');
  switch (parts.size()) {
  case 1: {
    package = parts[0];
    auto last_slash = package.find_last_of('/');
    if (last_slash != std::string_view::npos) {
      // //absl/strings to be interpreted as //absl/strings:strings
      target = package.substr(last_slash + 1);
    } else if (!package.empty() && package[0] == '@') {
      // we just have a toplevel, e.g. @jsonhpp
      target = package.substr(1);
    } else {
      package = "";  // Package without delimiter or package.
      target = str;
    }
    break;
  }
  case 2: {
    package = parts[0];
    target = parts[1];
    break;
  }
  default:  //
    return std::nullopt;
  }
  if (package.empty()) {
    return BazelTarget(context, target);
  }
  auto package_part = BazelPackage::ParseFrom(package);
  if (!package_part.has_value()) {
    return std::nullopt;
  }
  BazelPackage parsed_package = package_part.value();
  if (parsed_package.project.empty()) {
    parsed_package.project = project;
  }
  return BazelTarget(parsed_package, target);
}

static std::string_view PackageLastElement(const BazelPackage &p) {
  auto pos = p.path.find_last_of('/');
  if (pos != std::string::npos) {
    return std::string_view(p.path).substr(pos + 1);
  }
  if (!p.path.empty()) {
    return p.path;
  }
  if (!p.project.empty()) {
    return std::string_view(p.project).substr(1);
  }
  return "";
}

std::string BazelTarget::ToString() const {
  if (PackageLastElement(package) == target_name) {
    if (package.path.empty()) {
      return package.project;
    }
    return package.ToString();  // target==package -> compact representation.
  }
  return absl::StrCat(package.ToString(), ":", target_name);
}

std::string BazelTarget::ToStringRelativeTo(
  const BazelPackage &other_package) const {
  if (other_package != package) return ToString();  // regular handling.
  return absl::StrCat(":", target_name);
}

BazelPattern::BazelPattern() : kind_(MatchKind::kAlwaysMatch) {}

std::optional<BazelPattern> BazelPattern::ParseVisibility(
  std::string_view pattern, const BazelPackage &context) {
  if (pattern == "//visibility:public") {
    return BazelPattern();  // always match
  }
  if (pattern == "//visibility:private") {
    auto visibility_context = BazelTarget::ParseFrom("", context);
    if (!visibility_context.has_value()) return std::nullopt;
    return BazelPattern(*visibility_context, MatchKind::kAllTargetInPackage,
                        nullptr);
  }
  // HACK for now: until we understand package_groups, let everything that
  // does not look like a pattern be always-match
  if (!pattern.ends_with("...") && !pattern.ends_with("__")) {
    return BazelPattern();  // always match, essentially //visibility:public
  }
  return ParseFrom(pattern, context);
}

std::optional<BazelPattern> BazelPattern::ParseFrom(std::string_view pattern) {
  const BazelPackage empty_context("", "");
  return ParseFrom(pattern, empty_context);
}

static std::unique_ptr<RE2> GlobbingToRE2(std::string_view glob) {
  std::string assembled;
  bool is_first = true;
  for (;;) {
    const size_t pos = glob.find_first_of('*');
    if (pos == std::string_view::npos) break;
    absl::StrAppend(&assembled, RE2::QuoteMeta(glob.substr(0, pos)));
    if (is_first || pos > 0) {  // suppress multiple ** in a row.
      absl::StrAppend(&assembled, ".*");
    }
    glob = glob.substr(pos + 1);
    is_first = false;
  }
  absl::StrAppend(&assembled, glob);
  return std::make_unique<RE2>(assembled);
}

std::optional<BazelPattern> BazelPattern::ParseFrom(
  std::string_view pattern, const BazelPackage &context) {
  auto target = BazelTarget::ParseFrom(pattern, context);
  if (!target.has_value()) return std::nullopt;

  std::unique_ptr<RE2> regex;
  BazelTarget target_pattern = target.value();
  MatchKind kind = MatchKind::kExact;
  if (target_pattern.target_name == "__pkg__" ||  // typical in visibility
      target_pattern.target_name == "all" ||      // typical in cmd line
      target_pattern.target_name == "*") {        // trivial glob pattern
    target_pattern.target_name.clear();
    kind = MatchKind::kAllTargetInPackage;
  } else if (target_pattern.target_name == "__subpackages__") {  // visibility
    target_pattern.target_name.clear();
    kind = MatchKind::kRecursive;
  } else if (target_pattern.package.path == "..." ||
             target_pattern.package.path.ends_with("/...")) {
    std::string &p = target_pattern.package.path;
    if (p.size() >= 3) {
      p.resize(p.size() > 3 ? p.size() - 4 : p.size() - 3);
    }
    target_pattern.target_name.clear();
    kind = MatchKind::kRecursive;
  } else if (target_pattern.target_name == "...") {  // toplevel project match
    if (!target_pattern.package.path.empty()) {
      // The following should probably not be needed.
      return std::nullopt;  // Don't allow external packages.
    }
    target_pattern.target_name.clear();
    kind = MatchKind::kRecursive;
  } else if (target_pattern.target_name.contains('*')) {
    // Allow simplified globbing pattern.
    regex = GlobbingToRE2(target_pattern.target_name);
    if (!regex->ok()) {
      std::cerr << "Pattern issue " << regex->error() << "\n";
      return std::nullopt;
    }
    target_pattern.target_name.clear();
    kind = MatchKind::kTargetRegex;
  } else {
    kind = MatchKind::kExact;
  }

  return BazelPattern(target_pattern, kind, std::move(regex));
}

BazelPattern::BazelPattern(BazelTarget pattern, MatchKind kind,
                           std::unique_ptr<RE2> regex)
    : match_pattern_(std::move(pattern)),
      regex_pattern_(std::move(regex)),
      kind_(kind) {}

bool BazelPattern::Match(const BazelTarget &target) const {
  switch (kind_) {
  case MatchKind::kAlwaysMatch:  //
    return true;
  case MatchKind::kExact: {
    return target == match_pattern_;
  }
  case MatchKind::kTargetRegex:
    if (target.package != match_pattern_.package) {
      return false;
    }
    return RE2::FullMatch(target.target_name, *regex_pattern_);
  case MatchKind::kAllTargetInPackage: {
    return target.package == match_pattern_.package;
  }
  case MatchKind::kRecursive: return Match(target.package);
  }
  return false;
}

bool BazelPattern::Match(const BazelPackage &package) const {
  switch (kind_) {
  case MatchKind::kAlwaysMatch:  //
    return true;
  case MatchKind::kExact:
  case MatchKind::kTargetRegex:  // Target matches don't affect package match.
  case MatchKind::kAllTargetInPackage: {
    return package == match_pattern_.package;
  }
  case MatchKind::kRecursive: {
    if (package.project != match_pattern_.package.project) {
      return false;
    }
    const std::string &me = match_pattern_.package.path;
    if (me.empty()) return true;
    const std::string &to_match = package.path;
    return to_match.starts_with(me) && (me.length() == to_match.length() ||
                                        to_match.at(me.length()) == '/');
  }
  }
  return false;
}

}  // namespace bant
