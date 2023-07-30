/* -*- c++ -*- */
#ifndef PARSER_H
#define PARSER_H

#include <string_view>
#include <string>
#include <cstdint>
#include <cassert>

#include "arena.h"

class Visitor;

class Node {
public:
  virtual ~Node() = default;
  virtual void Accept(Visitor *v) = 0;
  virtual bool is_identifier() const { return false; }
};

class Scalar : public Node {
public:
  enum ScalarType { kInt, kString };

  virtual std::string_view AsString() { return ""; }
  virtual int64_t AsInt() { return 0; }

  virtual void Accept(Visitor *v);
  virtual ScalarType type() = 0;
};

class StringScalar : public Scalar {
public:
  static StringScalar *FromLiteral(Arena *arena, std::string_view literal);

  std::string_view AsString() final { return value_; }
  ScalarType type() final { return kString; }

private:
  StringScalar(std::string_view value) : value_(value) {}

  std::string_view value_;
};

class IntScalar : public Scalar {
public:
  static IntScalar *FromLiteral(Arena *arena, std::string_view literal);

  virtual int64_t AsInt() { return value_; }
  ScalarType type() final { return kInt; }

private:
  IntScalar(int64_t value) : value_(value) {}

  int64_t value_;
};

class Identifier : public Node {
public:
  // Needs to be owned outside. Typically the region in the
  // original file, that way it allows us report file location.
  Identifier(std::string_view id) : id_(id) {}
  const std::string_view id() const { return id_; }
  void Accept(Visitor *v) override;
  bool is_identifier() const final { return true; }

private:
  std::string_view id_;
};

class BinNode : public Node {
protected:
  BinNode(Node *lhs, Node *rhs) : left_(lhs), right_(rhs) {}

public:
  Node *left() { return left_; }
  Node *right() { return right_; }

protected:
  Node *left_;
  Node *right_;
};

// Few binops currently: '+', '-', ':' (mapping op).
class BinOpNode : public BinNode {
public:
  BinOpNode(Node *lhs, Node *rhs, char op) : BinNode(lhs, rhs), op_(op) {}
  void Accept(Visitor *v) override;
  char op() const { return op_; }

private:
  const char op_;
};

// List, maps and tuples are all lists.
class List : public Node {
public:
  enum Type {
    kList, kMap, kTuple
  };
  // To keep allocation simple, this is just a plain linked list.
  class Element : public BinNode {
  public:
    Element(Node *value) : BinNode(value, nullptr) {}
    Element* next() { return static_cast<Element*>(right_); }
    void SetNext(Element *e) { right_ = e; }
    void Accept(Visitor *) final { assert(0); } // Never called.
  };

  List(Type t) : type_(t) {}

  Element *first() { return first_; }

  void Append(Node *value) {
    Element *element = new Element(value);
    if (!first_) first_ = element;
    if (last_) last_->SetNext(element);
    last_ = element;
  }

  void Accept(Visitor *v) override;
  Type type() { return type_; }

private:
  const Type type_;
  Element *first_ = nullptr;
  Element *last_ = nullptr;
};

// Simple assignment: the only allowed lvalue is an identifier.
class Assignment : public BinNode {
public:
  Assignment(Identifier *identifier, Node *value)
    : BinNode(identifier, value) {}

  StringScalar *identifier() { return static_cast<StringScalar*>(left_); }
  Node *value() { return right_; }

  void Accept(Visitor *v) final;
};

// Function call.
class FunCall : public BinNode {
public:
  FunCall(Identifier *identifier, List *argument_list)
    : BinNode(identifier, argument_list) {
    assert(argument_list->type() == List::Type::kTuple);
  }
  Identifier *identifier() { return static_cast<Identifier*>(left_); }
  List *argument() { return static_cast<List*>(right_); }
  void Accept(Visitor *v) final;
};

class Visitor {
public:
  ~Visitor() = default;
  virtual void VisitAssignment(Assignment *) = 0;
  virtual void VisitFunCall(FunCall *) = 0;
  virtual void VisitList(List *) = 0;

  virtual void VisitBinOpNode(BinOpNode *) = 0;
  virtual void VisitScalar(Scalar *) = 0;  // Leaf.
  virtual void VisitIdentifier(Identifier *) = 0;  // Leaf.
};

class BaseVisitor : public Visitor {
public:
  void VisitAssignment(Assignment *a) override {
    if (a->right()) a->right()->Accept(this);
  }
  void VisitFunCall(FunCall *f) override {
    if (f->right()) f->right()->Accept(this);
  }
  void VisitList(List *l) override {
    for (List::Element *e = l->first(); e; e = e->next()) {
      if (e->left()) e->left()->Accept(this);
    }
  }
  void VisitBinOpNode(BinOpNode *b) override {
    if (b->left()) b->left()->Accept(this);
    if (b->right()) b->right()->Accept(this);
  }
  void VisitScalar(Scalar *) override {}  // Leaf
  void VisitIdentifier(Identifier *) override {}  // Leaf
};

class PrintVisitor : public BaseVisitor {
public:
  PrintVisitor(std::ostream &out) : out_(out) {}
  void VisitAssignment(Assignment *a) override;
  void VisitFunCall(FunCall *f) override;
  void VisitList(List *l) override;

  void VisitBinOpNode(BinOpNode *b) override;
  void VisitScalar(Scalar *s) override;
  void VisitIdentifier(Identifier *i) override;

private:
  int indent_ = 0;
  std::ostream &out_;
};

inline void Assignment::Accept(Visitor *v) { v->VisitAssignment(this); }
inline void FunCall::Accept(Visitor *v) { v->VisitFunCall(this); }
inline void List::Accept(Visitor *v) { v->VisitList(this); }
inline void BinOpNode::Accept(Visitor *v) { v->VisitBinOpNode(this); }
inline void Scalar::Accept(Visitor *v) { v->VisitScalar(this); }
inline void Identifier::Accept(Visitor *v) { v->VisitIdentifier(this); }

#endif // PARSER_H
