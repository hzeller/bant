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

#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/parsed-project.h"
#include "bant/frontend/substitute-copy.h"
#include "bant/session.h"
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

class MacroSubstitutor : public BaseNodeReplacementVisitor {
 public:
  explicit MacroSubstitutor(Session &session, ParsedProject *project)
      : session_(session), project_(project) {}

  Node *VisitFunCall(FunCall *f) final {
    const NestCounter c(&nest_level_);
    if (nest_level_ != 1) return BaseNodeReplacementVisitor::VisitFunCall(f);
    Node *macro = project_->FindMacro(f->identifier()->id());
    if (!macro) return f;  // No such macro, function is left as-is.
    ++substitution_count_;
    if (session_.flags().verbose > 1) {
      session_.info() << project_->Loc(f->identifier()->id())
                      << " Expanded macro " << f->identifier()->id() << "()\n";
    }
    const query::KwMap call_params = query::ExtractKwArgs(f);
    return VariableSubstituteCopy(macro, project_->arena(), call_params);
  }

  int substitution_count() const { return substitution_count_; }

 private:
  Session &session_;
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
  MacroSubstitutor substitutor(session, project);
  Node *result = ast->Accept(&substitutor);
  substitute_stats.count += substitutor.substitution_count();
  return result;
}
}  // namespace bant
