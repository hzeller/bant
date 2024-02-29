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

#ifndef BANT_AST_H_
#define BANT_AST_H_

#include <cassert>
#include <cstdint>
#include <ostream>
#include <string_view>

#include "arena-container.h"
#include "arena.h"
#include "scanner.h"

namespace bant {
class Visitor;
class Identifier;
class Scalar;
class List;
class BinOpNode;

// Constructors are not public, only accessible via Arena. Use Arena::New()
// for all nodes.
// All nodes are only composed of trivially destructable components so that
// we do not have to call destructors.

class Node {
 public:
  virtual ~Node() = default;
  virtual void Accept(Visitor *v) = 0;

  // Poor man's RTTI (also: cheaper). Return non-null if of that type.
  virtual Identifier *CastAsIdentifier() { return nullptr; }
  virtual Scalar *CastAsScalar() { return nullptr; }
  virtual List *CastAsList() { return nullptr; }
  virtual BinOpNode *CastAsBinOp() { return nullptr; }
};

class Scalar : public Node {
 public:
  enum ScalarType { kInt, kString };

  virtual std::string_view AsString() const { return ""; }
  virtual int64_t AsInt() const { return 0; }

  void Accept(Visitor *v) override;
  Scalar *CastAsScalar() final { return this; }

  virtual ScalarType type() = 0;
};

class StringScalar : public Scalar {
 public:
  static StringScalar *FromLiteral(Arena *arena, std::string_view literal);

  // Note: quotes around are removed, but potential escaping internally is
  // preserved in this view and points to the original span in the file.
  // Depending on is_raw(), the consumer can make unescape decisions.
  std::string_view AsString() const final { return value_; }

  // This is a raw string, i.e. all escape characters shall not be interpreted.
  bool is_raw() const { return is_raw_; }
  bool is_triple_quoted() const { return is_triple_quoted_; }

  ScalarType type() final { return kString; }

 private:
  friend class Arena;
  StringScalar(std::string_view value, bool is_triple_quoted, bool is_raw)
      : value_(value), is_triple_quoted_(is_triple_quoted), is_raw_(is_raw) {}

  std::string_view value_;
  bool is_triple_quoted_;
  bool is_raw_;
};

class IntScalar : public Scalar {
 public:
  static IntScalar *FromLiteral(Arena *arena, std::string_view literal);

  int64_t AsInt() const final { return value_; }
  ScalarType type() final { return kInt; }

 private:
  friend class Arena;
  explicit IntScalar(int64_t value) : value_(value) {}

  int64_t value_;
};

class Identifier : public Node {
 public:
  std::string_view id() const { return id_; }

  void Accept(Visitor *v) override;
  Identifier *CastAsIdentifier() final { return this; }

 private:
  friend class Arena;

  // Needs to be owned outside. Typically the region in the
  // original file, that way it allows us report file location.
  explicit Identifier(std::string_view id) : id_(id) {}

  std::string_view id_;
};

class UnaryExpr : public Node {
 public:
  Node *node() { return node_; }
  TokenType op() const { return op_; }

  void Accept(Visitor *v) final;

 protected:
  friend class Arena;
  explicit UnaryExpr(TokenType op, Node *n) : node_(n), op_(op) {}
  Node *node_;
  const TokenType op_;
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

// Generally some tree element that takes two nodes
// Arithmetic: '+', '-', '*', '/'
// Comparison: '==', '!=', '<', '<=', '>', '>='
// Special: ':' (mapping), '.' (scoped call),
//          'for' (in list comprehension), 'in' (operator and in for loop)
//          '[' Array access.
// Operator is just the corresponding Token.
class BinOpNode : public BinNode {
 public:
  void Accept(Visitor *v) override;
  TokenType op() const { return op_; }

  BinOpNode *CastAsBinOp() final { return this; }

 private:
  friend class Arena;
  BinOpNode(Node *lhs, Node *rhs, TokenType op) : BinNode(lhs, rhs), op_(op) {}
  const TokenType op_;
};

// List, maps and tuples are all lists.
class List : public Node {
 public:
  enum class Type { kList, kMap, kTuple };

  Type type() const { return type_; }
  size_t size() const { return list_.size(); }

  void Append(Arena *arena, Node *value) { list_.Append(value, arena); }
  ArenaDeque<Node *>::const_iterator begin() const { return list_.begin(); }
  ArenaDeque<Node *>::const_iterator end() const { return list_.end(); }

  void Accept(Visitor *v) override;
  List *CastAsList() final { return this; }

 private:
  friend class Arena;
  explicit List(Type t) : type_(t) {}

  const Type type_;
  ArenaDeque<Node *> list_;
};

// List comprehension for the given type (not only List, but also Map or tuple)
class ListComprehension : public Node {
 public:
  BinOpNode *for_node() { return for_node_; }
  List::Type type() const { return type_; }

  void Accept(Visitor *v) override;

 private:
  friend class Arena;
  ListComprehension(List::Type type, Node *for_node)
      : type_(type), for_node_(for_node->CastAsBinOp()) {
    assert(for_node_ != nullptr);
    assert(for_node_->op() == TokenType::kFor);
  }

  List::Type type_;
  BinOpNode *for_node_;
};

class Ternary : public Node {
 public:
  Ternary(Node *condition, Node *positive, Node *negative)
      : condition_(condition), positive_(positive), negative_(negative) {}

  Node *condition() { return condition_; }
  Node *positive() { return positive_; }
  Node *negative() { return negative_; }

  void Accept(Visitor *v) override;

 private:
  Node *condition_;
  Node *positive_;
  Node *negative_;
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
  virtual void VisitUnaryExpr(UnaryExpr *) = 0;
  virtual void VisitListComprehension(ListComprehension *) = 0;
  virtual void VisitTernary(Ternary *t) = 0;

  virtual void VisitScalar(Scalar *) = 0;          // Leaf.
  virtual void VisitIdentifier(Identifier *) = 0;  // Leaf.

  // Utility function: if node exists, walk and return 'true'.
  inline bool WalkNonNull(Node *node) {
    if (node) node->Accept(this);
    return node;
  }
};

// Simple implementation: recursively walk the whole AST.
class BaseVisitor : public Visitor {
 public:
  void VisitAssignment(Assignment *a) override { WalkNonNull(a->right()); }
  void VisitFunCall(FunCall *f) override { WalkNonNull(f->right()); }
  void VisitList(List *l) override {
    for (Node *node : *l) {
      WalkNonNull(node);
    }
  }
  void VisitUnaryExpr(UnaryExpr *e) override { WalkNonNull(e->node()); }
  void VisitBinOpNode(BinOpNode *b) override {
    WalkNonNull(b->left());
    WalkNonNull(b->right());
  }
  void VisitListComprehension(ListComprehension *lh) override {
    WalkNonNull(lh->for_node());
  }
  void VisitTernary(Ternary *t) override {
    WalkNonNull(t->condition());
    WalkNonNull(t->positive());
    WalkNonNull(t->negative());
  }

  void VisitScalar(Scalar *) override {}          // Leaf
  void VisitIdentifier(Identifier *) override {}  // Leaf
};

class PrintVisitor : public BaseVisitor {
 public:
  explicit PrintVisitor(std::ostream &out) : out_(out) {}
  void VisitAssignment(Assignment *a) override;
  void VisitFunCall(FunCall *f) override;
  void VisitList(List *l) override;

  void VisitUnaryExpr(UnaryExpr *e) override;
  void VisitBinOpNode(BinOpNode *b) override;
  void VisitListComprehension(ListComprehension *lh) override;
  void VisitTernary(Ternary *t) override;

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
inline void UnaryExpr::Accept(Visitor *v) { v->VisitUnaryExpr(this); }
inline void BinOpNode::Accept(Visitor *v) { v->VisitBinOpNode(this); }
inline void ListComprehension::Accept(Visitor *v) {
  v->VisitListComprehension(this);
}
inline void Ternary::Accept(Visitor *v) { v->VisitTernary(this); }
inline void Scalar::Accept(Visitor *v) { v->VisitScalar(this); }
inline void Identifier::Accept(Visitor *v) { v->VisitIdentifier(this); }

}  // namespace bant
#endif  // BANT_AST_H_
