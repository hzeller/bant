/* -*- c++ -*- */
#ifndef BANT_AST_H
#define BANT_AST_H

#include <cassert>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

#include "arena-container.h"
#include "arena.h"

class Visitor;

// Constructors are not public, only accessible via Arena. Use Arena::New()
// for all nodes.
// All nodes are only composed of trivially destructable components so that
// we do not have to call destructors.

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
  friend class Arena;
  StringScalar(std::string_view value) : value_(value) {}

  std::string_view value_;
};

class IntScalar : public Scalar {
 public:
  static IntScalar *FromLiteral(Arena *arena, std::string_view literal);

  virtual int64_t AsInt() { return value_; }
  ScalarType type() final { return kInt; }

 private:
  friend class Arena;
  IntScalar(int64_t value) : value_(value) {}

  int64_t value_;
};

class Identifier : public Node {
 public:
  const std::string_view id() const { return id_; }
  void Accept(Visitor *v) override;
  bool is_identifier() const final { return true; }

 private:
  friend class Arena;

  // Needs to be owned outside. Typically the region in the
  // original file, that way it allows us report file location.
  Identifier(std::string_view id) : id_(id) {}

  std::string_view id_;
};

class BinNode : public Node {
 public:
  Node *left() { return left_; }
  Node *right() { return right_; }

 protected:
  BinNode(Node *lhs, Node *rhs) : left_(lhs), right_(rhs) {}
  Node *left_;
  Node *right_;
};

// Few binops currently: '+', '-', ':' (mapping op).
class BinOpNode : public BinNode {
 public:
  void Accept(Visitor *v) override;
  char op() const { return op_; }

 private:
  friend class Arena;
  BinOpNode(Node *lhs, Node *rhs, char op) : BinNode(lhs, rhs), op_(op) {}
  const char op_;
};

// List, maps and tuples are all lists.
class List : public Node {
 public:
  enum Type { kList, kMap, kTuple };

  void Append(Arena *arena, Node *value) { list_.Append(value, arena); }

  void Accept(Visitor *v) override;
  Type type() { return type_; }

  ArenaDeque<Node *>::const_iterator begin() const { return list_.begin(); }
  ArenaDeque<Node *>::const_iterator end() const { return list_.end(); }

 private:
  friend class Arena;
  List(Type t) : type_(t) {}

  const Type type_;
  ArenaDeque<Node *> list_;
};

// Simple assignment: the only allowed lvalue is an identifier.
class Assignment : public BinNode {
 public:
  Identifier *identifier() { return static_cast<Identifier *>(left_); }
  Node *value() { return right_; }

  void Accept(Visitor *v) final;

 private:
  friend class Arena;
  Assignment(Identifier *identifier, Node *value)
      : BinNode(identifier, value) {}
};

// Function call.
class FunCall : public BinNode {
 public:
  Identifier *identifier() { return static_cast<Identifier *>(left_); }
  List *argument() { return static_cast<List *>(right_); }
  void Accept(Visitor *v) final;

 private:
  friend class Arena;
  FunCall(Identifier *identifier, List *argument_list)
      : BinNode(identifier, argument_list) {
    assert(argument_list->type() == List::Type::kTuple);
  }
};

class Visitor {
 public:
  ~Visitor() = default;
  virtual void VisitAssignment(Assignment *) = 0;
  virtual void VisitFunCall(FunCall *) = 0;
  virtual void VisitList(List *) = 0;
  virtual void VisitBinOpNode(BinOpNode *) = 0;

  virtual void VisitScalar(Scalar *) = 0;          // Leaf.
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
    for (Node *node : *l) {
      if (node) node->Accept(this);
    }
  }
  void VisitBinOpNode(BinOpNode *b) override {
    if (b->left()) b->left()->Accept(this);
    if (b->right()) b->right()->Accept(this);
  }
  void VisitScalar(Scalar *) override {}          // Leaf
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

std::ostream &operator<<(std::ostream &o, Node *n);

inline void Assignment::Accept(Visitor *v) { v->VisitAssignment(this); }
inline void FunCall::Accept(Visitor *v) { v->VisitFunCall(this); }
inline void List::Accept(Visitor *v) { v->VisitList(this); }
inline void BinOpNode::Accept(Visitor *v) { v->VisitBinOpNode(this); }
inline void Scalar::Accept(Visitor *v) { v->VisitScalar(this); }
inline void Identifier::Accept(Visitor *v) { v->VisitIdentifier(this); }

#endif  // BANT_AST_H
