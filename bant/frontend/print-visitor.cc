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

#include "bant/frontend/print-visitor.h"

#include <ostream>
#include <string>
#include <string_view>

#include "bant/frontend/ast.h"
#include "bant/frontend/scanner.h"
#include "re2/re2.h"

// TODO: put this in some other place.
struct Colors {
  std::string_view bold;
  std::string_view assignment_lhs;
  std::string_view reset;

  std::string_view highlight;
  std::string_view reset_hightlight;
};

static constexpr Colors kColor = {
  .bold = "\033[1m",
  .assignment_lhs = "\033[35m",
  .reset = "\033[0m",

  .highlight = "\033[7m",
  .reset_hightlight = "\033[27m",
};

namespace bant {
void PrintVisitor::PrintMaybeHighlight(std::string_view print_str) {
  const char *last_end = print_str.data();
  std::string_view highlight;
  if (highlight_re_) {
    while (RE2::FindAndConsume(&print_str, *highlight_re_, &highlight)) {
      out_ << std::string_view(last_end, highlight.data() - last_end);
      if (do_color_) out_ << kColor.highlight;
      out_ << highlight;
      if (do_color_) out_ << kColor.reset_hightlight;
      any_highlight_ = true;
      last_end = print_str.data();
    }
  }
  out_ << std::string_view(last_end, print_str.end() - last_end);
}

void PrintVisitor::VisitFunCall(FunCall *f) {
  if (do_color_) out_ << kColor.bold;
  PrintMaybeHighlight(f->identifier()->id());
  if (do_color_) out_ << kColor.reset;
  BaseVoidVisitor::VisitFunCall(f);
}

static void PrintListTypeOpen(List::Type t, std::ostream &out) {
  switch (t) {
  case List::Type::kList: out << "["; break;
  case List::Type::kMap: out << "{"; break;
  case List::Type::kTuple: out << "("; break;
  }
}
static void PrintListTypeClose(List::Type t, std::ostream &out) {
  switch (t) {
  case List::Type::kList: out << "]"; break;
  case List::Type::kMap: out << "}"; break;
  case List::Type::kTuple: out << ")"; break;
  }
}

void PrintVisitor::VisitList(List *l) {
  static constexpr int kIndentSpaces = 4;
  PrintListTypeOpen(l->type(), out_);
  const bool needs_multiline = (l->size() > 1);
  if (needs_multiline) out_ << "\n";
  indent_ += kIndentSpaces;
  bool is_first = true;
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

void PrintVisitor::VisitUnaryExpr(UnaryExpr *e) {
  out_ << e->op();
  if (e->op() == TokenType::kNot) out_ << " ";
  WalkNonNull(e->node());
}

void PrintVisitor::VisitAssignment(Assignment *a) {
  if (do_color_) out_ << kColor.assignment_lhs;
  WalkNonNull(a->left());
  if (do_color_) out_ << kColor.reset;
  out_ << " = ";
  WalkNonNull(a->right());
}

void PrintVisitor::VisitBinOpNode(BinOpNode *b) {
  WalkNonNull(b->left());
  if (b->op() == '.' || b->op() == '[') {
    out_ << b->op();  // No spacing around some operators.
  } else {
    out_ << " " << b->op() << " ";
  }
  WalkNonNull(b->right());
  if (b->op() == '[') {  // Array access is a BinOp with '[' as op.
    out_ << "]";
  }
}

void PrintVisitor::VisitListComprehension(ListComprehension *lh) {
  PrintListTypeOpen(lh->type(), out_);
  WalkNonNull(lh->for_node());
  PrintListTypeClose(lh->type(), out_);
}

void PrintVisitor::VisitTernary(Ternary *t) {
  WalkNonNull(t->positive());
  out_ << " if ";
  WalkNonNull(t->condition());
  if (t->negative()) {
    out_ << " else ";
    t->negative()->Accept(this);
  }
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
    // Minimal-effort quote char choosing. TODO: look if escaped
    const bool has_any_double_quote =
      str->AsString().find_first_of('"') != std::string_view::npos;
    const char quote_char = has_any_double_quote ? '\'' : '"';
    if (str->is_triple_quoted()) out_ << quote_char << quote_char;

    out_ << quote_char;
    PrintMaybeHighlight(str->AsString());
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
}  // namespace bant
