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

#include "bant/explore/query-utils.h"

#include <initializer_list>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "bant/frontend/ast.h"

namespace bant::query {
namespace {
// TODO: these of course need to be configurable. Ideally with a simple
// path query language.
class TargetFinder : public BaseVoidVisitor {
 public:
  TargetFinder(std::initializer_list<std::string_view> rules_of_interest,
               const TargetFindCallback &cb)
      : of_interest_(rules_of_interest), found_cb_(cb) {}

  void VisitFunCall(FunCall *f) final {
    if (in_relevant_call_ != Relevancy::kNotRelevant) {
      BaseVoidVisitor::VisitFunCall(f);  // Nesting.
      return;
    }
    in_relevant_call_ = IsRelevant(f->identifier()->id());
    if (in_relevant_call_ == Relevancy::kNotRelevant) return;

    current_ = {};
    current_.node = f;
    current_.rule = f->identifier()->id();
    for (Node *element : *f->argument()) {
      WalkNonNull(element);
    }
    if (in_relevant_call_ == Relevancy::kUserQuery) {
      InformCaller();
    }
    in_relevant_call_ = Relevancy::kNotRelevant;
  }

  // Assignment we see in a keyword argument inside a function call.
  void VisitAssignment(Assignment *a) final {
    switch (in_relevant_call_) {
    case Relevancy::kPackageInfo: ExtractPackageInfo(a); break;
    case Relevancy::kUserQuery: ExtractQueryInfo(a); break;
    default: break;
    }
  }

 private:
  enum class Relevancy {
    kNotRelevant,  // Not currently in any interesting function call.
    kUserQuery,    // Function call interesting because user asked for it.
    kPackageInfo   // Interesting because it contains package info.
  };

  // Relevant info we're interested in the package.
  void ExtractPackageInfo(Assignment *a) {
    if (!a->identifier() || !a->value()) return;
    const std::string_view lhs = a->identifier()->id();
    if (List *list = a->value()->CastAsList()) {
      if (lhs == "default_visibility") {
        package_default_visibility_ = list;
      }
    }
  }

  // Value extracted for the user query.
  void ExtractQueryInfo(Assignment *a) {
    if (!a->identifier() || !a->value()) return;
    const std::string_view lhs = a->identifier()->id();
    if (Scalar *scalar = a->value()->CastAsScalar()) {
      if (lhs == "name") {
        current_.name = scalar->AsString();
      } else if (lhs == "alwayslink") {
        current_.alwayslink = scalar->AsInt();
      } else if (lhs == "include_prefix") {
        current_.include_prefix = scalar->AsString();
      } else if (lhs == "strip_include_prefix") {
        current_.strip_include_prefix = scalar->AsString();
      } else if (lhs == "version") {
        current_.version = scalar->AsString();
      } else if (lhs == "repo_name") {
        current_.repo_name = scalar->AsString();
      } else if (lhs == "actual") {
        current_.actual = scalar->AsString();
      } else if (lhs == "deprecation") {
        current_.deprecation = scalar->AsString();
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
      } else if (lhs == "visibility") {
        current_.visibility = list;
      } else if (lhs == "textual_hdrs") {
        current_.textual_hdrs = list;
      }
    } else if (Identifier *id = a->value()->CastAsIdentifier()) {
      // If alwayslink has been a 'True' constant, the constant expression
      // eval will be flattening that to a scalar once implemented.
      // But until then, we need to check for the constant symbol manually.
      if (lhs == "alwayslink") {
        current_.alwayslink = (id->id() == "True");
      }
    }
  }

  void InformCaller() {
    if (current_.name.empty()) return;
    // If we never got a hdrs list (or couldn't read it because
    // it was a glob), assume this is an alwayslink library, so it wouldn't be
    // considered for removal by DWYU (e.g. :gtest_main)
    // TODO: figure out what the actual semantics is in bazel.
    if (current_.rule == "cc_library" &&
        (!current_.hdrs_list || current_.hdrs_list->empty())) {
      current_.alwayslink = true;
    }
    if (current_.visibility == nullptr) {
      current_.visibility = package_default_visibility_;
    }
    found_cb_(current_);
  }

  Relevancy IsRelevant(std::string_view name) const {
    if (name == "package") return Relevancy::kPackageInfo;
    if (of_interest_.empty()) return Relevancy::kUserQuery;
    return of_interest_.contains(name) ? Relevancy::kUserQuery
                                       : Relevancy::kNotRelevant;
  }

  // The package should come early in the file, so we should have gathered
  // the default visibility once we hit an actual rule.
  List *package_default_visibility_ = nullptr;

  // TODO: this assumes library call being a toplevel function; might need
  // stack here if nested (though we might just deal with that in a separate
  // transformation expanding list comprehensions).
  Result current_;

  Relevancy in_relevant_call_ = Relevancy::kNotRelevant;
  const absl::flat_hash_set<std::string_view> of_interest_;
  const TargetFindCallback &found_cb_;
};
}  // namespace

void FindTargets(Node *ast,
                 std::initializer_list<std::string_view> rules_of_interest,
                 const TargetFindCallback &cb) {
  TargetFinder(rules_of_interest, cb).WalkNonNull(ast);
}

void AppendStringList(List *list, std::vector<std::string_view> &append_to) {
  if (list == nullptr) return;
  for (Node *n : *list) {
    Scalar *scalar = n->CastAsScalar();
    if (!scalar) continue;
    if (const std::string_view str = scalar->AsString(); !str.empty()) {
      append_to.push_back(str);
    }
  }
}

std::vector<std::string_view> ExtractStringList(List *list) {
  std::vector<std::string_view> result;
  AppendStringList(list, result);
  return result;
}
}  // namespace bant::query
