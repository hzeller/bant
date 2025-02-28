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

#include <vector>

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
    if (right_prime == a->right()) return a;
    return Make<Assignment>(a->left(), right_prime, a->source_range());
  }

  Node *VisitFunCall(FunCall *f) override {
    // Not visiting the identifier; lhs regarded immutable.
    Node *right_prime = WalkNonNull(f->right());
    if (right_prime == f->right()) return f;
    return Make<FunCall>(f->identifier(), right_prime->CastAsList());
  }

  Node *VisitList(List *l) override {
    // TODO: replacements in lists are rare, so creating a temporary vector
    // for each of these is expensive. Instead, we should just go through the
    // list, and only once we reach the first difference, start building the
    // list up to that and continue building it .
    std::vector<Node *> new_elements;
    new_elements.reserve(l->size());
    bool all_same = true;
    for (Node *element : *l) {
      Node *element_prime = WalkNonNull(element);
      all_same &= (element == element_prime);
      new_elements.push_back(element_prime);
    }
    if (all_same) return l;
    List *result = Make<List>(l->type());
    for (Node *element : new_elements) {
      result->Append(arena_, element);
    }
    return result;
  }

  Node *VisitUnaryExpr(UnaryExpr *e) override {
    Node *node_prime = WalkNonNull(e->node());
    if (node_prime == e->node()) return e;
    return Make<UnaryExpr>(e->op(), node_prime);
  }

  Node *VisitBinOpNode(BinOpNode *b) override {
    Node *left_prime = WalkNonNull(b->left());
    Node *right_prime = WalkNonNull(b->right());
    if (left_prime == b->left() && right_prime == b->right()) {
      return b;  // no change.
    }
    return Make<BinOpNode>(left_prime, right_prime, b->op(), b->source_range());
  }

  Node *VisitListComprehension(ListComprehension *lc) override {
    // Dance around that we actually don't know if a BinOp type comes back.
    Node *for_node_prime = WalkNonNull(lc->for_node());
    if (for_node_prime == lc->for_node()) return lc;
    return Make<ListComprehension>(lc->type(), for_node_prime->CastAsBinOp());
  }

  Node *VisitTernary(Ternary *t) override {
    Node *condition_prime = WalkNonNull(t->condition());
    Node *positive_prime = WalkNonNull(t->positive());
    Node *negative_prime = WalkNonNull(t->negative());
    if (condition_prime == t->condition() && positive_prime == t->positive() &&
        negative_prime == t->negative()) {
      return t;
    }
    return Make<Ternary>(condition_prime, positive_prime, negative_prime);
  }

  Node *VisitIdentifier(Identifier *i) override {
    auto found = variables_.find(i->id());
    return found != variables_.end() ? found->second : i;
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
