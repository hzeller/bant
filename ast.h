// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#ifndef BANT_AST_H_
#define BANT_AST_H_

#include <cassert>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

#include "arena-container.h"
#include "arena.h"

namespace bant {
class Visitor;
class Identifier;
class Scalar;
class List;

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
};

class Scalar : public Node {
 public:
  enum ScalarType { kInt, kString };

  Scalar *CastAsScalar() final { return this; }
  virtual std::string_view AsString() { return ""; }
  virtual int64_t AsInt() { return 0; }

  void Accept(Visitor *v) override;
  virtual ScalarType type() = 0;
};

class StringScalar : public Scalar {
 public:
  static StringScalar *FromLiteral(Arena *arena, std::string_view literal);

  std::string_view AsString() final { return value_; }
  ScalarType type() final { return kString; }

 private:
  friend class Arena;
  explicit StringScalar(std::string_view value) : value_(value) {}

  std::string_view value_;
};

class IntScalar : public Scalar {
 public:
  static IntScalar *FromLiteral(Arena *arena, std::string_view literal);

  int64_t AsInt() final { return value_; }
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

class BinNode : public Node {
 public:
  Node *left() { return left_; }
  Node *right() { return right_; }

 protected:
  BinNode(Node *lhs, Node *rhs) : left_(lhs), right_(rhs) {}
  Node *left_;
  Node *right_;
};

// Few binops currently: '+', '-', ':' (mapping op), '.' (scoped call)
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

  List *CastAsList() final { return this; }
  void Append(Arena *arena, Node *value) { list_.Append(value, arena); }

  void Accept(Visitor *v) override;
  Type type() { return type_; }
  size_t size() const { return list_.size(); }

  ArenaDeque<Node *>::const_iterator begin() const { return list_.begin(); }
  ArenaDeque<Node *>::const_iterator end() const { return list_.end(); }

 private:
  friend class Arena;
  explicit List(Type t) : type_(t) {}

  const Type type_;
  ArenaDeque<Node *> list_;
};

class ListComprehension : public Node {
 public:
  ListComprehension(Node *pattern, List *variable_list, Node *source)
      : pattern_(pattern), variable_list_(variable_list), source_(source) {}
  Node *pattern() { return pattern_; }
  List *variable_list() { return variable_list_; }
  Node *source() { return source_; }

  void Accept(Visitor *v) override;

 private:
  Node *pattern_;
  List *variable_list_;
  Node *source_;
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

class BaseVisitor : public Visitor {
 public:
  void VisitAssignment(Assignment *a) override { WalkNonNull(a->right()); }
  void VisitFunCall(FunCall *f) override { WalkNonNull(f->right()); }
  void VisitList(List *l) override {
    for (Node *node : *l) {
      WalkNonNull(node);
    }
  }
  void VisitBinOpNode(BinOpNode *b) override {
    WalkNonNull(b->left());
    WalkNonNull(b->right());
  }
  void VisitListComprehension(ListComprehension *lh) override {
    WalkNonNull(lh->pattern());
    WalkNonNull(lh->variable_list());
    WalkNonNull(lh->source());
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
inline void BinOpNode::Accept(Visitor *v) { v->VisitBinOpNode(this); }
inline void ListComprehension::Accept(Visitor *v) {
  v->VisitListComprehension(this);
}
inline void Ternary::Accept(Visitor *v) { v->VisitTernary(this); }
inline void Scalar::Accept(Visitor *v) { v->VisitScalar(this); }
inline void Identifier::Accept(Visitor *v) { v->VisitIdentifier(this); }

}  // namespace bant
#endif  // BANT_AST_H_
