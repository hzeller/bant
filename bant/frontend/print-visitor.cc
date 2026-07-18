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

#include "bant/frontend/print-visitor.h"

#include <functional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bant/frontend/ast.h"
#include "bant/frontend/operator-precedence.h"
#include "bant/frontend/scanner.h"

namespace bant {

namespace {
class ScopedPrecedence {
 public:
  ScopedPrecedence(PrintVisitor::PrecedenceState *current, int node_precedence,
                   bool req)
      : current_(current), saved_state_(*current) {
    current_->level = node_precedence;
    current_->require_parens_if_equal = req;
  }
  ~ScopedPrecedence() { *current_ = saved_state_; }

  void Set(int precedence, bool parens_if_equal) {
    current_->level = precedence;
    current_->require_parens_if_equal = parens_if_equal;
  }

 private:
  PrintVisitor::PrecedenceState *const current_;
  const PrintVisitor::PrecedenceState saved_state_;
};

// TODO: put this in some other place. There is also color handling in
// util/term-color.h
struct Colors {
  std::string_view bold;
  std::string_view assignment_lhs;
  std::string_view reset;
};

// Helper to look up the precedence level of a given operator token.
// Returns kLowestPrecedence if the operator is not found in the precedence
// list.
static int GetPrecedenceLevel(TokenType op, bool is_unary = false) {
  for (int i = 0; i <= kLowestBinaryPrecedence; ++i) {
    if (kPrecedenceList[i].is_unary != is_unary) continue;
    for (const TokenType t : kPrecedenceList[i].tokens) {
      if (t == op) return i;
    }
  }
  return kLowestPrecedence;
}
}  // namespace

static constexpr Colors kColor = {
  .bold = "\033[1m",
  .assignment_lhs = "\033[35m",
  .reset = "\033[0m",
};

void PrintVisitor::RegisterStringScalarCallback(
  std::function<void(std::string_view)> cb) {
  string_scalar_callback_ = std::move(cb);
}

void PrintVisitor::VisitFunCall(FunCall *f) {
  if (do_color_) out_ << kColor.bold;
  out_ << f->identifier()->id();
  if (do_color_) out_ << kColor.reset;
  BaseVoidVisitor::VisitFunCall(f);
}

static void PrintListTypeOpen(List::Type t, std::ostream &out) {
  switch (t) {
  case List::Type::kList: out << "["; break;
  case List::Type::kMap: out << "{"; break;
  case List::Type::kTuple:
  case List::Type::kStruct: out << "("; break;
  }
}
static void PrintListTypeClose(List::Type t, std::ostream &out) {
  switch (t) {
  case List::Type::kList: out << "]"; break;
  case List::Type::kMap: out << "}"; break;
  case List::Type::kTuple:
  case List::Type::kStruct: out << ")"; break;
  }
}

void PrintVisitor::VisitList(List *l) {
  if (l->type() == List::Type::kStruct) {
    out_ << "struct";  // this way, it looks like a function call again.
  }
  static constexpr int kIndentSpaces = 4;
  PrintListTypeOpen(l->type(), out_);
  const bool needs_multiline = (l->size() > 1);
  if (needs_multiline) out_ << "\n";
  indent_ += kIndentSpaces;
  bool is_first = true;

  const ScopedPrecedence scope(&current_precedence_, kLowestPrecedence, false);

  for (Node *node : *l) {
    if (!is_first) out_ << ",\n";
    if (needs_multiline) out_ << std::string(indent_, ' ');
    if (!WalkNonNull(node)) {
      out_ << "NIL";
    }
    is_first = false;
  }

  // If a tuple only contains one element, then we need a final ','
  // to disambiguate from a parenthesized expression.
  if (l->type() == List::Type::kTuple && l->size() == 1) {
    out_ << ",";
  }

  indent_ -= kIndentSpaces;
  if (needs_multiline) {
    out_ << "\n" << std::string(indent_, ' ');
  }
  PrintListTypeClose(l->type(), out_);
}

bool PrintVisitor::CheckAndPrintParensOpen(int node_precedence) {
  const bool needs_parens = (current_precedence_.level < node_precedence) ||
                            (current_precedence_.level == node_precedence &&
                             current_precedence_.require_parens_if_equal);
  if (needs_parens) out_ << "(";
  return needs_parens;
}

void PrintVisitor::VisitUnaryExpr(UnaryExpr *e) {
  const int node_precedence = GetPrecedenceLevel(e->op(), true);
  const bool need_parens = CheckAndPrintParensOpen(node_precedence);

  out_ << e->op();
  if (e->op() == TokenType::kNot) out_ << " ";

  {
    const ScopedPrecedence scope(&current_precedence_, node_precedence, false);
    WalkNonNull(e->node());
  }

  if (need_parens) out_ << ")";
}

void PrintVisitor::VisitAssignment(Assignment *a) {
  if (do_color_) out_ << kColor.assignment_lhs;

  {
    const ScopedPrecedence scope(&current_precedence_, kLowestPrecedence,
                                 false);
    WalkNonNull(a->left());
    if (do_color_) out_ << kColor.reset;
    out_ << " = ";
    WalkNonNull(a->right());
  }
}

void PrintVisitor::VisitBinOpNode(BinOpNode *b) {
  const int node_prec = b->op() == '[' ? 1 : GetPrecedenceLevel(b->op(), false);
  const bool need_parens = CheckAndPrintParensOpen(node_prec);

  ScopedPrecedence scope(&current_precedence_, node_prec, false);
  WalkNonNull(b->left());

  if (b->op() == '[') {
    out_ << "[";
    scope.Set(kLowestPrecedence, false);  // New context inside brackets
    WalkNonNull(b->right());
    out_ << "]";
  } else if (b->op() == '.') {
    out_ << ".";
    scope.Set(node_prec, true);
    WalkNonNull(b->right());
  } else {
    out_ << " " << b->op() << " ";
    scope.Set(node_prec, true);
    WalkNonNull(b->right());
  }

  if (need_parens) out_ << ")";
}

void PrintVisitor::VisitListComprehension(ListComprehension *lh) {
  PrintListTypeOpen(lh->type(), out_);

  std::vector<BinOpNode *> spine;
  Node *curr = lh->for_node();
  while (curr) {
    if (BinOpNode *b = curr->CastAsBinOp()) {
      if (b->op() == TokenType::kFor || b->op() == TokenType::kIf) {
        spine.push_back(b);
        curr = b->left();
        continue;
      }
    }
    break;
  }

  const ScopedPrecedence scope(&current_precedence_, kLowestPrecedence, false);

  // Print the inner subject expression
  WalkNonNull(curr);

  // We collected spine from root to leaf, so the outmost loop is at
  // spine.front(). Because the AST is built outside-in to match Python
  // sequence, the spine perfectly aligns with textual order (e.g. For_x, If_x,
  // For_y, If_y).
  for (BinOpNode *b : spine) {
    if (b->op() == TokenType::kFor) {
      out_ << " for ";
    } else {
      out_ << " if ";
    }
    WalkNonNull(b->right());
  }

  PrintListTypeClose(lh->type(), out_);
}

void PrintVisitor::VisitTernary(Ternary *t) {
  const int node_precedence = kTernaryPrecedence;
  const bool need_parens = CheckAndPrintParensOpen(node_precedence);

  ScopedPrecedence scope(&current_precedence_, node_precedence, true);
  WalkNonNull(t->positive());

  out_ << " if ";
  scope.Set(node_precedence, true);
  WalkNonNull(t->condition());

  if (t->negative()) {
    out_ << " else ";
    scope.Set(node_precedence, false);
    WalkNonNull(t->negative());
  }

  if (need_parens) out_ << ")";
}

void PrintVisitor::VisitScalar(Scalar *s) {
  if (s->type() == Scalar::ScalarType::kInt) {
    if (s->AsString().empty()) {
      out_ << s->AsInt();
    } else {
      out_ << s->AsString();  // Keep original representation intact if avail.
    }
  } else {
    const StringScalar *str = static_cast<StringScalar *>(s);
    if (str->is_raw()) out_ << "r";
    const char quote_char = str->quote_char();

    if (str->is_triple_quoted()) out_ << quote_char << quote_char;
    out_ << quote_char;
    if (string_scalar_callback_) string_scalar_callback_(s->AsString());
    out_ << str->AsString();
    out_ << quote_char;
    if (str->is_triple_quoted()) out_ << quote_char << quote_char;
  }
}

void PrintVisitor::VisitIdentifier(Identifier *i) { out_ << i->id(); }

std::ostream &operator<<(std::ostream &o, Node *n) {
  if (!PrintVisitor(o).WalkNonNull(n)) {
    o << "NIL";
  }
  return o;
}

std::string ToString(Node *n) {
  std::stringstream print;
  print << n;
  return print.str();
}

}  // namespace bant
