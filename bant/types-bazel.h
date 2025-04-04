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

#ifndef BANT_TYPES_BAZEL_
#define BANT_TYPES_BAZEL_

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bant/workspace.h"
#include "re2/re2.h"

namespace bant {
class BazelTarget;

// Something like //foo/bar or @baz//foo/bar
struct BazelPackage {
  BazelPackage() = default;
  BazelPackage(std::string_view project, std::string_view path)
      : project(project), path(path) {}

  // Parse and create package if possible.
  static std::optional<BazelPackage> ParseFrom(std::string_view str);

  // Given a name of a target without package, reutrn a fully qualified target.
  std::optional<BazelTarget> QualifiedTarget(std::string_view name) const;

  std::string project;  // either empty, or something like @foo_bar_baz
  std::string path;     // path relative to project w/o leading/trailing '/'

  std::string ToString() const;  // @project//package/path

  // Assemble filename relative to the path.
  std::string QualifiedFile(std::string_view relative_file) const;

  // Assemble filename including potential prefix if it is located in an
  // external project.
  std::string FullyQualifiedFile(const BazelWorkspace &workspace,
                                 std::string_view relative_file) const;

  auto operator<=>(const BazelPackage &o) const = default;
};

inline std::ostream &operator<<(std::ostream &o, const BazelPackage &p) {
  return o << p.ToString();
}

// TODO: this should probably be called BazelLabel which seems to be the common
// to refer to it in the bazel documentation.
class BazelTarget {
 public:
  // Parse target from string. Both forms //foo/bar:baz and :baz are
  // supported. The latter case is canonicalized by adding the context package.
  static std::optional<BazelTarget> ParseFrom(std::string_view str,
                                              const BazelPackage &context);

  BazelTarget() = default;

  BazelPackage package;
  std::string target_name;  // e.g. a library

  std::string ToString() const;

  // More compact printing of a path if we are already in that package.
  std::string ToStringRelativeTo(const BazelPackage &other_package) const;

  auto operator<=>(const BazelTarget &o) const = default;

 private:
  friend BazelPackage;
  // Make sure we only use the ParseFrom(). Not always needed, but this way
  // it is harder to accidentally have broken targets.
  BazelTarget(BazelPackage package, std::string_view target)
      : package(std::move(package)), target_name(target) {}
};

inline std::ostream &operator<<(std::ostream &o, const BazelTarget &t) {
  return o << t.ToString();
}

// An oracle to ask if bazel targets or packages are included in some
// pattern this matcher represents.
class BazelTargetMatcher {
 public:
  ~BazelTargetMatcher() = default;

  // if false, nothing will be filtered, match will unconditionally return true.
  virtual bool HasFilter() const = 0;

  virtual bool Match(const BazelTarget &target) const = 0;
  virtual bool Match(const BazelPackage &package) const = 0;
};

// A bazel pattern such as //foo/... or //foo:all
// But also for visibility rules :__pkg__ and :__subpackages__ as they are
// essentially the same.
// TODO: there are also relative patterns without leading '//' and also things
//  like ...:all. Note with that, path() will need to be replace with something/
//  yielding globbing results.
class BazelPattern final : public BazelTargetMatcher {
 public:
  ~BazelPattern() = default;

  // The default constructed pattern always matches anything.
  BazelPattern();

  // Factory to parse BazelPattern that is returned as value if successful.
  static std::optional<BazelPattern> ParseFrom(std::string_view pattern);

  // Very similar to ParseFrom, but taking sligth visibility pattern differences
  // into account.
  static std::optional<BazelPattern> ParseVisibility(
    std::string_view pattern, const BazelPackage &context);

  const std::string &path() const { return match_pattern_.package.path; }
  const std::string &project() const { return match_pattern_.package.project; }

  bool is_recursive() const {
    return (kind_ == MatchKind::kRecursive || kind_ == MatchKind::kAlwaysMatch);
  }

  // -- BazelTargetMatcher interface
  bool HasFilter() const final { return kind_ != MatchKind::kAlwaysMatch; }
  bool Match(const BazelTarget &target) const final;
  bool Match(const BazelPackage &package) const final;

 private:
  enum class MatchKind {
    kExact,
    kTargetRegex,
    kAllTargetInPackage,
    kRecursive,
    kAlwaysMatch
  };

  static std::optional<BazelPattern> ParseFrom(std::string_view pattern,
                                               const BazelPackage &context);

  BazelPattern(BazelTarget pattern, MatchKind kind, std::unique_ptr<RE2> regex);

  BazelTarget match_pattern_;
  std::shared_ptr<RE2> regex_pattern_;  // shared: makes it copyable.
  MatchKind kind_;
};

class BazelPatternBundle final : public BazelTargetMatcher {
 public:
  void AddPattern(const BazelPattern &p) { patterns_.emplace_back(p); }
  void Finish() {
    has_filter_ = !patterns_.empty();
    if (!has_filter_) {
      // Make it provide a regular recursive BazelPattern to make things
      // work seamlessly.
      patterns_.emplace_back();
    }
  }

  const std::vector<BazelPattern> &patterns() const { return patterns_; };

  // -- BazelTargetMatcher interface
  bool HasFilter() const final { return has_filter_; }
  bool Match(const BazelTarget &target) const final;
  bool Match(const BazelPackage &package) const final;

 private:
  // TODO: maybe also match negative patterns that 'subtract' ? Then store
  // tuple here <bool, BazelPattern>.
  std::vector<BazelPattern> patterns_;
  bool has_filter_ = false;
};
}  // namespace bant

#endif  // BANT_TYPES_BAZEL_
