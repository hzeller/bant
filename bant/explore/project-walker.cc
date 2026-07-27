// bant - Bazel Navigation Tool
// Copyright (C) 2026 Henner Zeller <h.zeller@acm.org>
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

#include "bant/explore/project-walker.h"

#include <initializer_list>
#include <string_view>

#include "bant/explore/query-utils.h"
#include "bant/frontend/parsed-project.h"
#include "bant/types-bazel.h"

namespace bant {
void ProjectWalker::FindTargets(
  std::initializer_list<std::string_view> rules_of_interest,
  const ProjectWalker::Callback &callback) const {
  for (const auto &[_, build_file] : project_.ParsedFiles()) {
    if (!build_file->ast) continue;
    const BazelPackage &package = build_file->package;
    query::FindTargets(build_file->ast, rules_of_interest,
                       [&](const query::Result &param) {
                         auto rule_label = package.QualifiedTarget(param.name);
                         if (!rule_label.has_value()) return;
                         callback(package, *rule_label, param);
                       });
  }
}

void ProjectWalker::FindTargetsWithPattern(
  const BazelTargetMatcher &pattern,
  std::initializer_list<std::string_view> rules_of_interest,
  const ProjectWalker::Callback &callback) const {
  for (const auto &[_, build_file] : project_.ParsedFiles()) {
    if (!build_file->ast) continue;
    const BazelPackage &package = build_file->package;
    if (!pattern.Match(package)) continue;
    query::FindTargets(build_file->ast, rules_of_interest,
                       [&](const query::Result &param) {
                         auto rule_label = package.QualifiedTarget(param.name);
                         if (!rule_label.has_value()) return;
                         if (!pattern.Match(*rule_label)) return;
                         callback(package, *rule_label, param);
                       });
  }
}
}  // namespace bant
