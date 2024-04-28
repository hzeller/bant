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

#include "bant/frontend/ast.h"

#include <charconv>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>

#include "bant/frontend/scanner.h"
#include "bant/util/arena.h"

namespace bant {
IntScalar *IntScalar::FromLiteral(Arena *arena, std::string_view literal) {
  int64_t val = 0;
  auto result = std::from_chars(literal.begin(), literal.end(), val);
  if (result.ec != std::errc{}) {
    return nullptr;
  }
  return arena->New<IntScalar>(val);
}

StringScalar *StringScalar::FromLiteral(Arena *arena,
                                        std::string_view literal) {
  bool is_raw = false;
  bool is_triple_quoted = false;
  if (literal[0] == 'r' || literal[0] == 'R') {
    is_raw = true;
    literal.remove_prefix(1);
  }
  if (literal.length() >= 6 && literal.substr(0, 3) == R"(""")") {
    is_triple_quoted = true;
    literal = literal.substr(3);
    literal.remove_suffix(3);
  } else {
    literal = literal.substr(1);
    literal.remove_suffix(1);
  }

  // The string itself might still contain escaping characters, so anyone
  // using it might need to unescape it.
  // Within the Scalar, we keep the original string_view so that it is possible
  // to report location using the LineColumnMap.
  return arena->New<StringScalar>(literal, is_triple_quoted, is_raw);
}

void PrintVisitor::VisitAssignment(Assignment *a) {
  out_ << a->identifier()->id() << " = ";
  BaseVisitor::VisitAssignment(a);
}

void PrintVisitor::VisitFunCall(FunCall *f) {
  out_ << f->identifier()->id();
  BaseVisitor::VisitFunCall(f);
  out_ << "\n" << std::string(indent_, ' ');
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

void PrintVisitor::VisitBinOpNode(BinOpNode *b) {
  WalkNonNull(b->left());
  if (b->op() == '.') {
    out_ << b->op();  // No spacing around dot operator.
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
    out_ << s->AsInt();
  } else {
    const StringScalar *str = static_cast<StringScalar *>(s);
    if (str->is_raw()) out_ << "r";
    // Minimal-effort quote char choosing. TODO: look if escaped
    const bool has_any_double_quote =
      str->AsString().find_first_of('"') != std::string_view::npos;
    const char quote_char = has_any_double_quote ? '\'' : '"';
    if (str->is_triple_quoted()) out_ << quote_char << quote_char;
    out_ << quote_char << str->AsString() << quote_char;
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
