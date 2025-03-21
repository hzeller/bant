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

#include "bant/frontend/macro-substitutor.h"

#include <string_view>

#include "absl/log/check.h"
#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/substitute-copy.h"
#include "bant/session.h"
#include "bant/util/arena.h"
#include "bant/util/stat.h"

namespace bant {
namespace {
class NestCounter {
 public:
  explicit NestCounter(int *value) : value_(value) { ++*value_; }
  ~NestCounter() { --*value_; }

 private:
  int *const value_;
};

class MacroForwardArgs : public BaseNodeReplacementVisitor {
 public:
  explicit MacroForwardArgs(ParsedProject *project, List *kwargs)
      : arena_(project->arena()), kwargs_(kwargs) {}

  Node *VisitFunCall(FunCall *f) final {
    List *new_args = arena_->New<List>(List::Type::kTuple);
    for (Node *item : *kwargs_) {  // Coming from the macro call
      new_args->Append(arena_, item);
    }
    for (Node *item : *f->argument()) {  // already in the function
      new_args->Append(arena_, item);
    }
    return arena_->New<FunCall>(f->identifier(), new_args);
  }

 private:
  Arena *const arena_;
  List *const kwargs_;
};

class MacroSubstitutor : public BaseNodeReplacementVisitor {
 public:
  static constexpr std::string_view kForwardMacro = "bant_forward_args";

  explicit MacroSubstitutor(ParsedProject *project) : project_(project) {}

  Node *VisitFunCall(FunCall *f) final {
    const NestCounter c(&nest_level_);
    if (nest_level_ != 1) return BaseNodeReplacementVisitor::VisitFunCall(f);
    Node *macro = project_->FindMacro(f->identifier()->id());
    if (!macro) return f;  // No such macro, function is left as-is.
    ++substitution_count_;

    // If forwarding macro, we fill all function calls inside with our kwargs.
    if (FunCall *maybe_forward = macro->CastAsFunCall();
        maybe_forward && maybe_forward->identifier()->id() == kForwardMacro) {
      List *forward_arg = maybe_forward->argument();
      // OK to check, built-in macros must not have issues.
      CHECK(!forward_arg->empty()) << "Expect at least one call";
      // Multiple calls will automatically become a tuple
      macro = forward_arg->size() == 1 ? forward_arg->at(0) : forward_arg;
      List *kwargs_to_forward = f->argument();
      MacroForwardArgs forwarder(project_, kwargs_to_forward);
      return macro->Accept(&forwarder);
    }

    // Otherwise we take the kwargs as a set of variables that are resolved
    // inside.
    const query::KwMap call_params = query::ExtractKwArgs(f);
    return VariableSubstituteCopy(macro, project_->arena(), call_params);
  }

  int substitution_count() const { return substitution_count_; }

 private:
  ParsedProject *const project_;
  int nest_level_ = 0;
  int substitution_count_ = 0;
};
}  // namespace

Node *MacroSubstitute(Session &session, ParsedProject *project, Node *ast) {
  if (!ast) return ast;
  bant::Stat &substitute_stats =
    session.GetStatsFor("  - substituting", "macros");
  const ScopedTimer timer(&substitute_stats.duration);
  MacroSubstitutor substitutor(project);
  Node *result = ast->Accept(&substitutor);
  substitute_stats.count += substitutor.substitution_count();
  return result;
}
}  // namespace bant
