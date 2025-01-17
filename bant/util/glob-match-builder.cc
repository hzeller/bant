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

#include "bant/util/glob-match-builder.h"

#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "re2/re2.h"

namespace bant {
namespace {
// A matcher that delegates to direct matches or regexp depending on context.
class PathMatcher {
 public:
  PathMatcher(std::string_view regex,
              absl::flat_hash_set<std::string> &&match_set)
      : pattern_re_(regex), verbatim_match_(std::move(match_set)) {}

  bool Match(std::string_view s) const {
    return verbatim_match_.contains(s) || RE2::FullMatch(s, pattern_re_);
  }

 private:
  RE2 pattern_re_;
  absl::flat_hash_set<std::string> verbatim_match_;
};

// Needs to be shared, as RE2 can't be copied and std::function also can't
// things std::move()'ed into it.
static std::shared_ptr<PathMatcher> MakeFilenameMatcher(
  const std::set<std::string> &patterns) {
  std::vector<std::string> re_or_patterns;
  absl::flat_hash_set<std::string> verbatim_match;
  for (const std::string &p : patterns) {
    if (absl::StrContains(p, '*')) {
      const std::string escape_special = RE2::QuoteMeta(p);  // quote everything
      re_or_patterns.emplace_back(  // ... then unquote the pattern back
        absl::StrReplaceAll(escape_special, {{R"(\*\*\/)", ".*/?"},  //
                                             {R"(\*)", "[^/]*"}}));
    } else {
      verbatim_match.insert(p);  // Simple and fast.
    }
  }
  return std::make_shared<PathMatcher>(absl::StrJoin(re_or_patterns, "|"),
                                       std::move(verbatim_match));
}

static std::shared_ptr<PathMatcher> MakeDirectoryMatcher(
  const std::set<std::string> &patterns) {
  std::set<std::string> re_or_patterns;  // sorted maybe beneficial for RE2
  absl::flat_hash_set<std::string> verbatim_match;
  for (std::string_view p : patterns) {
    const size_t last_slash = p.find_last_of('/');
    if (last_slash == std::string_view::npos) {
      verbatim_match.insert("");
      continue;
    }
    p = p.substr(0, last_slash);  // Only directories for patterns
    // TODO: is it allowed to have patterns like '**.txt' with the '**' not
    // in directory ? Because then we just snipped it off and it won't work...
    if (absl::StrContains(p, '*')) {
      // We need to convert file-patterns into directory patterns. Directories
      // only go up to the last element and we need to match a prefix of
      // directory elments. So foo/bar/baz needs to match foo(/bar(/baz)?)?
      const std::string escape_special = RE2::QuoteMeta(p);  // quote everything
      std::string dir_pattern =  // ... then unquote the pattern back
        absl::StrReplaceAll(escape_special, {{R"(\*\*)", ".*/?"},  //
                                             {R"(\*)", "[^/]*"}});

      // Now, make this a prefix-match by grouping each part.
      const int parens =
        absl::StrReplaceAll({{R"(\/)", R"((\/)"}}, &dir_pattern);
      for (int i = 0; i < parens; ++i) {
        dir_pattern.append(")?");
      }
      re_or_patterns.insert(dir_pattern);
    } else {
      size_t pos = 0;
      for (;;) {
        const size_t next = p.find_first_of('/', pos);
        if (next == std::string::npos) break;
        verbatim_match.insert(std::string{p.substr(0, next)});
        pos = next + 1;
      }
      verbatim_match.insert(std::string{p});
    }
  }
  return std::make_shared<PathMatcher>(absl::StrJoin(re_or_patterns, "|"),
                                       std::move(verbatim_match));
}
}  // namespace

// Public interface
void GlobMatchBuilder::AddIncludePattern(std::string_view pattern) {
  include_pattern_.insert(std::string{pattern});
}
void GlobMatchBuilder::AddExcludePattern(std::string_view pattern) {
  exclude_pattern_.insert(std::string{pattern});
}

std::function<bool(std::string_view)>
GlobMatchBuilder::BuildFileMatchPredicate() {
  auto include = MakeFilenameMatcher(include_pattern_);
  auto exclude = MakeFilenameMatcher(exclude_pattern_);
  return [=](std::string_view s) {
    if (!include->Match(s)) return false;
    return !exclude->Match(s);
  };
}

std::function<bool(std::string_view)>
GlobMatchBuilder::BuildDirectoryMatchPredicate() {
  auto dir_matcher = MakeDirectoryMatcher(include_pattern_);
  return [=](std::string_view s) { return dir_matcher->Match(s); };
}

}  // namespace bant
