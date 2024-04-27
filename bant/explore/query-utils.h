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

#ifndef BANT_QUERY_UTILS_
#define BANT_QUERY_UTILS_

#include <functional>
#include <string_view>

#include "bant/frontend/ast.h"

namespace bant {
namespace query {
// Typical parameters for for binaries, cc_libraries rules and other
// 'function call' things we look at. Starts to get a bit crowded.
// They typically have a name, and various lists with sources and dependencies.
struct Result {
  FunCall *node = nullptr;
  std::string_view rule;  // rule, sucha as cc_library, cc_binary,...
  std::string_view name;
  std::string_view version;    // in bazel_dep()
  std::string_view repo_name;  // in bazel_dep()
  List *srcs_list = nullptr;
  List *hdrs_list = nullptr;
  List *deps_list = nullptr;
  List *outs_list = nullptr;              // genrule.
  List *includes_list = nullptr;          // various ways ...
  std::string_view include_prefix;        // ... to manipulate the path ...
  std::string_view strip_include_prefix;  // ... files from hdrs are found.
  bool alwayslink = false;
};

// Callback of a query.
using TargetFindCallback = std::function<void(const Result &)>;

// Walk the "ast" and find all the targets that match any of the given
// "rules_of_interest" names (such as 'cc_library'). If list empty: match all.
// Provides callback with all the relevant information gathered in a
// convenient struct.
// All string views are pointing to the original data, so it is possible to
// get detailed line/column information for user display.
void FindTargets(Node *ast,
                 std::initializer_list<std::string_view> rules_of_interest,
                 const TargetFindCallback &cb);

// Utility function: extract list of strings from list-node and append to
// vector. The original string-views are preserved, so can be used to recover
// location in file.
void ExtractStringList(List *list, std::vector<std::string_view> &append_to);
std::vector<std::string_view> ExtractStringList(List *list);

}  // namespace query
}  // namespace bant

#endif  // BANT_QUERY_UTILS_
