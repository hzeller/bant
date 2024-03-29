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

#include <iostream>
#include <string_view>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace bant {
std::string BazelPackage::ToString() const {
  return absl::StrCat(project, "//", path);
}

std::string BazelPackage::QualifiedFile(std::string_view relative_file) const {
  if (path.empty()) return std::string(relative_file);
  return absl::StrCat(path, "/", relative_file);
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
  } else {
    project = "";
    path = str;
  }
  while (!path.empty() && path.front() == '/') path.remove_prefix(1);
  while (!path.empty() && path.back() == '/') path.remove_suffix(1);
  if (path.find("//") != std::string_view::npos) {
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
  std::string_view project = context.project;
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

std::optional<BazelPattern> BazelPattern::ParseFrom(std::string_view pattern) {
  const BazelPackage empty_context("", "");
  auto target = BazelTarget::ParseFrom(pattern, empty_context);
  if (!target.has_value()) return std::nullopt;

  BazelTarget target_pattern = target.value();
  MatchKind kind = MatchKind::kExact;
  if (target_pattern.target_name == "__pkg__" ||
      target_pattern.target_name == "all") {
    target_pattern.target_name.clear();
    kind = MatchKind::kAllInPackage;
  } else if (target_pattern.target_name == "__subpackages__") {
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
  } else if (target_pattern.target_name == "...") {
    if (!target_pattern.package.path.empty()) {
      return std::nullopt;
    }
    target_pattern.target_name.clear();
    kind = MatchKind::kRecursive;
  } else {
    kind = MatchKind::kExact;
  }

  return BazelPattern(target_pattern, kind);
}

BazelPattern::BazelPattern(const BazelTarget &pattern, MatchKind kind)
    : target_pattern_(pattern), kind_(kind) {}

bool BazelPattern::Match(const BazelTarget &target) const {
  switch (kind_) {
  case MatchKind::kAlwaysMatch:  //
    return true;
  case MatchKind::kExact: return target == target_pattern_;
  case MatchKind::kAllInPackage:
    return target.package == target_pattern_.package;
  case MatchKind::kRecursive: return Match(target.package);
  }
  return false;
}

bool BazelPattern::Match(const BazelPackage &target) const {
  switch (kind_) {
  case MatchKind::kAlwaysMatch:  //
    return true;
  case MatchKind::kExact:
  case MatchKind::kAllInPackage: return target == target_pattern_.package;
  case MatchKind::kRecursive: {
    if (target.project != target_pattern_.package.project) {
      return false;
    }
    const std::string &me = target_pattern_.package.path;
    if (me.empty()) return true;
    const std::string &to_match = target.path;
    return to_match.starts_with(me) && (me.length() == to_match.length() ||
                                        to_match.at(me.length()) == '/');
  }
  }
  return false;
}

}  // namespace bant
