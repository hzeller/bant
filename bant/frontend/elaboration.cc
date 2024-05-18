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

#include "bant/frontend/elaboration.h"

#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "bant/frontend/ast.h"
#include "bant/frontend/parsed-project.h"
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

class SimpleElaborator : public BaseNodeReplacementVisitor {
 public:
  Node *VisitFunCall(FunCall *f) final {
    const NestCounter c(&nest_level_);
    return BaseNodeReplacementVisitor::VisitFunCall(f);
  }

  Node *VisitList(List *l) final {
    // TODO: maybe increase nest level here, but need to make sure
    // toplevel project would be at level 0 (as file-ast is a list)
    return BaseNodeReplacementVisitor::VisitList(l);
  }

  Node *VisitAssignment(Assignment *a) final {
    Node *result = BaseNodeReplacementVisitor::VisitAssignment(a);
    if (nest_level_ == 0) {
      global_variables_[a->identifier()->id()] = a->value();
    }
    return result;
  }

  Node *VisitIdentifier(Identifier *i) final {
    auto found = global_variables_.find(i->id());
    return found != global_variables_.end() ? found->second : i;
  }

 private:
  int nest_level_ = 0;
  absl::flat_hash_map<std::string_view, Node *> global_variables_;
};
}  // namespace

Node *Elaborate(ParsedProject *project, Node *ast) {
  SimpleElaborator elaborator;
  return elaborator.WalkNonNull(ast);
}

void Elaborate(Session &session, ParsedProject *project) {
  bant::Stat &elab_stats = session.GetStatsFor("Elaborated", "files");
  const ScopedTimer timer(&elab_stats.duration);

  for (const auto &[_, build_file] : project->ParsedFiles()) {
    Node *const result = Elaborate(project, build_file->ast);
    CHECK_EQ(result, build_file->ast) << "Toplevel should never be replaced";
    ++elab_stats.count;
  }
}
}  // namespace bant
