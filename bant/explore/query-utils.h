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

#ifndef BANT_QUERY_UTILS_
#define BANT_QUERY_UTILS_

#include <functional>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "bant/frontend/ast.h"

namespace bant::query {
// A Smörgåsbord of keyword parameters found for binaries, cc_libraries rules
// and other 'calls' to rules we look at. Starts to get a bit crowded (but
// is also cheap, as a instance is re-used and only passed by reference).
// Rules typically have a name, and various lists with sources and dependencies.
struct Result {
  FunCall *node = nullptr;
  std::string_view rule;  // rule, sucha as cc_library, cc_binary,...
  std::string_view name;
  std::string_view actual;  // for aliases.
  std::string_view deprecation;
  List *srcs_list = nullptr;
  List *hdrs_list = nullptr;
  List *textual_hdrs = nullptr;
  List *public_hdrs = nullptr;
  List *deps_list = nullptr;
  List *data_list = nullptr;
  List *tools_list = nullptr;
  List *outs_list = nullptr;              // genrule.
  List *visibility = nullptr;             // from rule or default_visibility
  List *includes_list = nullptr;          // various ways ...
  std::string_view include_prefix;        // ... to manipulate the path ...
  std::string_view strip_include_prefix;  // ... files from hdrs are found.
  std::string_view strip_import_prefix;   // ... similar, used in proto_library
  bool alwayslink = false;
  bool testonly = false;
  bool bant_skip_dependency_check = false;  // No dwyu; used in builtin macros.
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

// Same as above, but allow the name to be empty.
void FindTargetsAllowEmptyName(
  Node *ast, std::initializer_list<std::string_view> rules_of_interest,
  const TargetFindCallback &cb);

// Get all the keyword arguments fron the function call.
// TODO: location of KwMap type in this header might be too specific.
using KwMap = absl::flat_hash_map<std::string_view, Node *>;
KwMap ExtractKwArgs(FunCall *fun);

// Given a function call (e.g. from a rule invocation), extract the node
// assigned to the given keyword.
Node *FindKWArg(FunCall *fun, std::string_view keyword);

// Find string argument if available on that keyword, otherwise nullopt.
std::optional<std::string_view> FindKWArgAsStringView(FunCall *fun,
                                                      std::string_view keyword);

// Utility function: extract list of non-empty strings from list-node and
// return as vector.
// The original string-views are preserved, so can be used to recover the
// location in file.
std::vector<std::string_view> ExtractStringList(List *list);

// Similar to ExtractStringList(), but append to vector.
void AppendStringList(List *list, std::vector<std::string_view> &append_to);

}  // namespace bant::query

#endif  // BANT_QUERY_UTILS_
