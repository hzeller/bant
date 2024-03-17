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

#include "bant/util/query-utils.h"

#include <initializer_list>

#include "absl/container/flat_hash_set.h"
#include "bant/frontend/ast.h"

namespace bant {
namespace query {
namespace {
// TODO: these of course need to be configurable. Ideally with a simple
// path query language.
class TargetFinder : public BaseVisitor {
 public:
  TargetFinder(std::initializer_list<std::string_view> rules_of_interest,
               const TargetFindCallback &cb)
      : of_interest_(rules_of_interest), found_cb_(cb) {}

  bool IsRelevant(std::string_view name) const {
    if (of_interest_.empty()) return true;  // matchall
    return of_interest_.contains(name);
  }

  void VisitFunCall(FunCall *f) final {
    if (in_relevant_call_) {
      return BaseVisitor::VisitFunCall(f);  // Nesting.
    }
    in_relevant_call_ = IsRelevant(f->identifier()->id());
    if (!in_relevant_call_) return;  // Nothing interesting beyond here.
    current_ = {};
    current_.node = f;
    current_.rule = f->identifier()->id();
    for (Node *element : *f->argument()) {
      WalkNonNull(element);
    }
    InformCaller();
    in_relevant_call_ = false;
  }

  void VisitAssignment(Assignment *a) final {
    if (!in_relevant_call_) return;  // can prune walk here
    if (!a->identifier() || !a->value()) return;
    const std::string_view lhs = a->identifier()->id();
    if (Scalar *scalar = a->value()->CastAsScalar(); scalar) {
      if (lhs == "name") {
        current_.name = scalar->AsString();
      } else if (lhs == "alwayslink") {
        // Even if the follwing was given as 'True' constant, the constant
        // expression eval will have flattened that to a scalar.
        current_.alwayslink = scalar->AsInt();
      } else if (lhs == "include_prefix") {
        current_.include_prefix = scalar->AsString();
      } else if (lhs == "strip_include_prefix") {
        current_.strip_include_prefix = scalar->AsString();
      } else if (lhs == "version") {
        current_.version = scalar->AsString();
      } else if (lhs == "repo_name") {
        current_.repo_name = scalar->AsString();
      }
    } else if (List *list = a->value()->CastAsList()) {
      if (lhs == "hdrs") {
        current_.hdrs_list = list;
      } else if (lhs == "srcs") {
        current_.srcs_list = list;
      } else if (lhs == "deps") {
        current_.deps_list = list;
      } else if (lhs == "includes") {
        current_.includes_list = list;
      } else if (lhs == "outs") {
        current_.outs_list = list;
      }
    }
  }

 private:
  void InformCaller() {
    if (current_.name.empty()) return;
    // If we never got a hdrs list (or couldn't read it because
    // it was a glob), assume this is an alwayslink library, so it wouldn't be
    // considered for removal by DWYU (e.g. :gtest_main)
    // TODO: figure out what the actual semantics is in bazel.
    if (current_.rule == "cc_library" && current_.hdrs_list == nullptr) {
      current_.alwayslink = true;
    }
    found_cb_(current_);
  }

  bool in_relevant_call_ = false;

  // TODO: this assumes library call being a toplevel function; might need
  // stack here if nested (maybe in tuples after for-expansion?)
  Result current_;

  const absl::flat_hash_set<std::string_view> of_interest_;
  const TargetFindCallback &found_cb_;
};
}  // namespace

void FindTargets(Node *ast,
                 std::initializer_list<std::string_view> rules_of_interest,
                 const TargetFindCallback &cb) {
  TargetFinder(rules_of_interest, cb).WalkNonNull(ast);
}

void ExtractStringList(List *list, std::vector<std::string_view> &append_to) {
  if (list == nullptr) return;
  for (Node *n : *list) {
    Scalar *scalar = n->CastAsScalar();
    if (!scalar) continue;
    if (std::string_view str = scalar->AsString(); !str.empty()) {
      append_to.push_back(str);
    }
  }
}

}  // namespace query
}  // namespace bant
