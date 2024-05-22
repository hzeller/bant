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

#include "bant/util/glob-match-builder.h"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "re2/re2.h"

namespace bant {
void GlobMatchBuilder::AddIncludePattern(std::string_view pattern) {
  AddPatternAsRegex(pattern, &include_pattern_);
}
void GlobMatchBuilder::AddExcludePattern(std::string_view pattern) {
  AddPatternAsRegex(pattern, &exclude_pattern_);
}

void GlobMatchBuilder::AddPatternAsRegex(std::string_view pattern,
                                         std::vector<std::string> *receiver) {
  const std::string escape_special = RE2::QuoteMeta(pattern);
  receiver->emplace_back(
    absl::StrReplaceAll(escape_special, {{R"(\*\*)", ".*"},  //
                                         {R"(\*)", "[^/]*"}}));
}

std::function<bool(std::string_view)>
GlobMatchBuilder::BuildFileMatchPredicate() {
  auto include_re = std::make_shared<RE2>(absl::StrJoin(include_pattern_, "|"));
  auto exclude_re = std::make_shared<RE2>(absl::StrJoin(exclude_pattern_, "|"));
  return [=](std::string_view s) {
    if (!RE2::FullMatch(s, *include_re)) {
      return false;
    }
    return !RE2::FullMatch(s, *exclude_re);
  };
}

std::function<bool(std::string_view)>
GlobMatchBuilder::BuildDirectoryMatchPredicate() {
  std::set<std::string> unique_patterns;

  // We need to convert file-patterns into directory patterns. Directories
  // only go up to the last element and we need to match a prefix of complete
  // directory elmeents. So foo/bar/baz needs to match foo(/bar(/baz)?)?
  // TODO: is it easily possible to derive a negative pattern ?
  for (const std::string_view file_pattern : include_pattern_) {
    std::string dir_pattern;
    if (file_pattern.ends_with(".*")) {
      dir_pattern = file_pattern;
    } else {
      auto last_slash_pos = file_pattern.rfind(R"(\/)");
      if (last_slash_pos != std::string_view::npos) {
        dir_pattern = file_pattern.substr(0, last_slash_pos);
      }
    }
    const int parens = absl::StrReplaceAll({{R"(\/)", R"((\/)"}}, &dir_pattern);
    for (int i = 0; i < parens; ++i) {
      dir_pattern.append(")?");
    }
    unique_patterns.insert(dir_pattern);
  }

  auto include_re = std::make_shared<RE2>(absl::StrJoin(unique_patterns, "|"));
  return [=](std::string_view s) { return RE2::FullMatch(s, *include_re); };
}

}  // namespace bant
