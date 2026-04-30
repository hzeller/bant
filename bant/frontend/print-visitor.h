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

#ifndef BANT_PRINT_VISITOR_H
#define BANT_PRINT_VISITOR_H

#include <ostream>

#include "bant/frontend/ast.h"
#include "bant/frontend/operator-precedence.h"

namespace bant {
class PrintVisitor : public BaseVoidVisitor {
 public:
  explicit PrintVisitor(std::ostream &out, bool do_color = false)
      : out_(out), do_color_(do_color) {}

  void VisitAssignment(Assignment *a) final;
  void VisitFunCall(FunCall *f) final;
  void VisitList(List *l) final;

  void VisitUnaryExpr(UnaryExpr *e) final;
  void VisitBinOpNode(BinOpNode *b) final;
  void VisitListComprehension(ListComprehension *lh) final;
  void VisitTernary(Ternary *t) final;

  void VisitScalar(Scalar *s) final;
  void VisitIdentifier(Identifier *i) final;

  struct PrecedenceState {
    // The current precedence level context (lower number = tighter binding).
    int level = kLowestPrecedence;

    // Whether parentheses are required if the child's precedence exactly
    // matches. Used to correctly enforce left-to-right or right-to-left
    // associativity.
    bool require_parens_if_equal = false;
  };

 private:
  bool CheckAndPrintParensOpen(int node_precedence);

  std::ostream &out_;
  const bool do_color_;
  int indent_ = 0;

  PrecedenceState current_precedence_;
};
}  // namespace bant

#endif  // BANT_PRINT_VISITOR_H
