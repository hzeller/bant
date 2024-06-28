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

#ifndef BANT_PRINT_VISITOR_H
#define BANT_PRINT_VISITOR_H

#include <ostream>

#include "bant/frontend/ast.h"
#include "re2/re2.h"

namespace bant {
class PrintVisitor : public BaseVoidVisitor {
 public:
  explicit PrintVisitor(std::ostream &out,
                        const RE2 *optional_highlight = nullptr,
                        bool do_color = false)
      : out_(out), highlight_re_(optional_highlight), do_color_(do_color) {}
  void VisitAssignment(Assignment *a) final;
  void VisitFunCall(FunCall *f) final;
  void VisitList(List *l) final;

  void VisitUnaryExpr(UnaryExpr *e) final;
  void VisitBinOpNode(BinOpNode *b) final;
  void VisitListComprehension(ListComprehension *lh) final;
  void VisitTernary(Ternary *t) final;

  void VisitScalar(Scalar *s) final;
  void VisitIdentifier(Identifier *i) final;

  bool any_highlight() const { return any_highlight_; }

 private:
  std::ostream &out_;
  const RE2 *const highlight_re_;
  const bool do_color_;

  int indent_ = 0;
  bool any_highlight_ = false;
};
}  // namespace bant

#endif  // BANT_PRINT_VISITOR_H
