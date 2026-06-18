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

#include "bant/explore/query-utils.h"

#include <initializer_list>
#include <optional>
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
               bool allow_empty_name, const TargetFindCallback &cb)
      : of_interest_(rules_of_interest),
        allow_empty_name_(allow_empty_name),
        found_cb_(cb) {}

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
    if (!a->lhs_maybe_identifier() || !a->value()) return;
    const std::string_view lhs = a->lhs_maybe_identifier()->id();
    if (List *list = a->value()->CastAsList()) {
      if (lhs == "default_visibility") {
        package_default_visibility_ = list;
      }
    }
  }

  // Value extracted for the user query.
  void ExtractQueryInfo(Assignment *a) {
    if (!a->lhs_maybe_identifier() || !a->value()) return;
    const std::string_view lhs = a->lhs_maybe_identifier()->id();
    if (const Scalar *const scalar = a->value()->CastAsScalar()) {
      if (lhs == "name") {
        current_.name = scalar->AsString();
      } else if (lhs == "alwayslink") {
        current_.alwayslink_scalar = scalar;
      } else if (lhs == "testonly") {
        current_.testonly = scalar->AsInt();
      } else if (lhs == "bant_skip_dependency_check") {
        current_.bant_skip_dependency_check = scalar->AsInt();
      } else if (lhs == "include_prefix") {
        current_.include_prefix = scalar->AsString();
      } else if (lhs == "strip_include_prefix") {
        current_.strip_include_prefix = scalar->AsString();
      } else if (lhs == "strip_import_prefix") {
        current_.strip_import_prefix = scalar->AsString();
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
      } else if (lhs == "implementation_deps") {
        current_.impl_deps_list = list;
      } else if (lhs == "data") {
        current_.data_list = list;
      } else if (lhs == "tools") {
        current_.tools_list = list;
      } else if (lhs == "includes") {
        current_.includes_list = list;
      } else if (lhs == "outs") {
        current_.outs_list = list;
      } else if (lhs == "visibility") {
        current_.visibility = list;
      } else if (lhs == "textual_hdrs") {
        current_.textual_hdrs = list;
      } else if (lhs == "public_hdrs") {
        current_.public_hdrs = list;
      } else if (lhs == "copts") {
        current_.copts = list;
      } else if (lhs == "defines") {
        current_.defines = list;
      } else if (lhs == "local_defines") {
        current_.local_defines = list;
      } else if (lhs == "tags") {
        current_.tags = list;
      }
    }
  }

  void InformCaller() {
    if (!allow_empty_name_ && current_.name.empty()) return;
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
  const bool allow_empty_name_;
  const TargetFindCallback &found_cb_;
};
}  // namespace

void FindTargets(Node *ast,
                 std::initializer_list<std::string_view> rules_of_interest,
                 const TargetFindCallback &cb) {
  TargetFinder(rules_of_interest, false, cb).WalkNonNull(ast);
}

void FindTargetsAllowEmptyName(
  Node *ast, std::initializer_list<std::string_view> rules_of_interest,
  const TargetFindCallback &cb) {
  TargetFinder(rules_of_interest, true, cb).WalkNonNull(ast);
}

KwMap ExtractKwArgs(FunCall *fun) {
  KwMap result;
  if (!fun->argument()) return result;
  for (Node *arg : *fun->argument()) {
    if (Assignment *assign = arg->CastAsAssignment()) {
      Identifier *id = assign->lhs_maybe_identifier();
      if (!id || !assign->value()) continue;
      result.emplace(id->id(), assign->value());
    }
  }
  return result;
}

Node *FindKWArg(FunCall *fun, std::string_view keyword) {
  if (!fun->argument()) return nullptr;
  for (Node *arg : *fun->argument()) {
    if (Assignment *assign = arg->CastAsAssignment()) {
      Identifier *id = assign->lhs_maybe_identifier();
      if (id && id->id() == keyword) return assign->value();
    }
  }
  return nullptr;
}

std::optional<std::string_view> FindKWArgAsStringView(
  FunCall *fun, std::string_view keyword) {
  Node *node = FindKWArg(fun, keyword);
  if (!node) return std::nullopt;
  const Scalar *const scalar = node->CastAsScalar();
  if (!scalar || scalar->type() != Scalar::ScalarType::kString) {
    return std::nullopt;
  }
  return scalar->AsString();
}

void AppendStringList(List *list, std::vector<std::string_view> &append_to) {
  if (list == nullptr) return;
  for (Node *n : *list) {
    if (!n) continue;  // Parse error of sorts.
    const Scalar *const scalar = n->CastAsScalar();
    if (!scalar) continue;
    if (const std::string_view str = scalar->AsString(); !str.empty()) {
      append_to.push_back(str);
    }
  }
}

std::vector<std::string_view> ExtractStringList(List *from_this) {
  std::vector<std::string_view> result;
  AppendStringList(from_this, result);
  return result;
}

std::vector<std::string_view> ExtractStringList(
  std::initializer_list<List *> from_these) {
  std::vector<std::string_view> result;
  for (List *from_this : from_these) {
    AppendStringList(from_this, result);
  }
  return result;
}
}  // namespace bant::query
