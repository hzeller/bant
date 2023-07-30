#include <ostream>

#include "ast.h"

void PrintVisitor::VisitAssignment(Assignment *a)  {
  out_ << a->identifier()->AsString() << " = ";
  BaseVisitor::VisitAssignment(a);
}

void PrintVisitor::VisitFunCall(FunCall *f)  {
  out_ << f->identifier()->id();
  BaseVisitor::VisitFunCall(f);
  out_ << "\n";
}

void PrintVisitor::VisitList(List *l)  {
  switch (l->type()) {
  case List::Type::kList: out_ << "["; break;
  case List::Type::kMap: out_ << "{"; break;
  case List::Type::kTuple: out_ << "("; break;
  }
  indent_ += 2;
  for (List::Element *e = l->first(); e; e = e->next()) {
    if (e->left()) {
      e->left()->Accept(this);
    } else {
      out_ << "NIL";
    }
    if (e->next()) out_ << ",\n" << std::string(indent_, ' ');
  }
  indent_ -= 2;
  switch (l->type()) {
  case List::Type::kList: out_ << "]"; break;
  case List::Type::kMap: out_ << "}"; break;
  case List::Type::kTuple: out_ << ")"; break;
  }
}

void PrintVisitor::VisitBinOpNode(BinOpNode *b)  {
  if (b->left()) b->left()->Accept(this);
  out_ << " " << b->op() << " ";
  if (b->right()) b->right()->Accept(this);
}

void PrintVisitor::VisitScalar(Scalar *s)  {
  if (s->type() == Scalar::kInt) {
    out_ << s->AsInt();
  } else {
    out_ << "\"" << s->AsString() << "\"";
  }
}

void PrintVisitor::VisitIdentifier(Identifier *i)  {
  out_ << i->id();
}
