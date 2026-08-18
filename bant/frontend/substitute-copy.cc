// bant - Bazel Navigation Tool
// Copyright (C) 2025 Henner Zeller <h.zeller@acm.org>
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

#include "bant/frontend/substitute-copy.h"

#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/util/arena.h"

namespace bant {
namespace {
class VariableSubstituteCopyVisitor : public NodeVisitor {
 public:
  VariableSubstituteCopyVisitor(const query::KwMap &variables, Arena *arena)
      : variables_(variables), arena_(arena) {}

  Node *VisitAssignment(Assignment *a) override {
    // Not visiting the identifier; lhs regarded immutable.
    Node *right_prime = WalkNonNull(a->right());
    return Make<Assignment>(a->left(), right_prime, a->source_range());
  }

  Node *VisitFunCall(FunCall *f) override {
    // Not visiting the identifier; lhs regarded immutable.
    Node *right_prime = WalkNonNull(f->right());
    return Make<FunCall>(f->identifier(), right_prime->CastAsList());
  }

  Node *VisitList(List *l) override {
    List *result = Make<List>(l->type());
    for (Node *const element : *l) {
      result->Append(arena_, WalkNonNull(element));
    }
    return result;
  }

  Node *VisitUnaryExpr(UnaryExpr *e) override {
    return Make<UnaryExpr>(e->op(), WalkNonNull(e->node()));
  }

  Node *VisitBinOpNode(BinOpNode *b) override {
    Node *const left_prime = WalkNonNull(b->left());
    Node *const right_prime =
      (b->op() == '.' && b->right() && b->right()->CastAsIdentifier())
        ? b->right()
        : WalkNonNull(b->right());
    return Make<BinOpNode>(left_prime, right_prime, b->op(), b->source_range());
  }

  Node *VisitListComprehension(ListComprehension *lc) override {
    Node *for_node_prime = WalkNonNull(lc->for_node());
    return Make<ListComprehension>(lc->type(), for_node_prime->CastAsBinOp());
  }

  Node *VisitTernary(Ternary *t) override {
    Node *const condition_prime = WalkNonNull(t->condition());
    Node *const positive_prime = WalkNonNull(t->positive());
    Node *const negative_prime = WalkNonNull(t->negative());
    return Make<Ternary>(condition_prime, positive_prime, negative_prime);
  }

  Node *VisitIdentifier(Identifier *i) override {
    auto found = variables_.find(i->id());
    if (found != variables_.end()) return found->second;
    return Make<Identifier>(i->id());
  }

  Node *VisitScalar(Scalar *s) override { return s; }  // identity.

 private:
  // Convenience factory creating in our Arena, forwarding to constructor.
  template <typename T, class... U>
  T *Make(U &&...args) {
    return arena_->New<T>(std::forward<U>(args)...);
  }

  const query::KwMap &variables_;
  Arena *arena_;
};
}  // namespace

Node *VariableSubstituteCopy(Node *ast, Arena *arena,
                             const query::KwMap &varmap) {
  VariableSubstituteCopyVisitor substitutor(varmap, arena);
  return ast->Accept(&substitutor);
}
}  // namespace bant
