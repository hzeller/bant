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

#include "ast.h"

#include <charconv>
#include <ostream>

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
                                        std::string_view literal,
                                        bool is_raw) {
  if (literal.length() >= 6 && literal.substr(0, 3) == "\"\"\"") {
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
  return arena->New<StringScalar>(literal, is_raw);
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

void PrintVisitor::VisitList(List *l) {
  static constexpr int kIndentSpaces = 4;
  switch (l->type()) {
  case List::Type::kList: out_ << "["; break;
  case List::Type::kMap: out_ << "{"; break;
  case List::Type::kTuple: out_ << "("; break;
  }
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
  indent_ -= kIndentSpaces;
  if (needs_multiline) {
    out_ << "\n" << std::string(indent_, ' ');
  }

  switch (l->type()) {
  case List::Type::kList: out_ << "]"; break;
  case List::Type::kMap: out_ << "}"; break;
  case List::Type::kTuple: out_ << ")"; break;
  }
}

void PrintVisitor::VisitBinOpNode(BinOpNode *b) {
  WalkNonNull(b->left());
  if (b->op() == '.') {
    out_ << b->op();  // No spacing around dot operator.
  } else {
    out_ << " " << b->op() << " ";
  }
  WalkNonNull(b->right());
}

void PrintVisitor::VisitListComprehension(ListComprehension *lh) {
  out_ << "[\n";
  WalkNonNull(lh->pattern());
  out_ << "\n" << std::string(indent_, ' ') << "for ";
  WalkNonNull(lh->variable_list());
  out_ << "\n" << std::string(indent_, ' ') << "in ";
  WalkNonNull(lh->source());
  out_ << "\n]";
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
  if (s->type() == Scalar::kInt) {
    out_ << s->AsInt();
  } else {
    const StringScalar *str = static_cast<StringScalar *>(s);
    if (str->is_raw()) out_ << "r";
    // Minimal-effort quote char choosing. TODO: look if escaped
    const bool has_any_double_quote =
      str->AsString().find_first_of('"') != std::string_view::npos;
    const char quote_char = has_any_double_quote ? '\'' : '"';
    out_ << quote_char << str->AsString() << quote_char;
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
