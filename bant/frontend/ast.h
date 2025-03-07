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

#ifndef BANT_AST_H_
#define BANT_AST_H_

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

#include "bant/frontend/scanner.h"
#include "bant/util/arena-container.h"
#include "bant/util/arena.h"

namespace bant {
class VoidVisitor;
class NodeVisitor;

class Assignment;
class BinOpNode;
class FunCall;
class Identifier;
class List;
class Scalar;

// Constructors are not public, only accessible via Arena. Use Arena::New()
// for all nodes.
// All nodes are only composed of trivially destructable components so that
// we do not have to call destructors when wiped from arena.
//
// Nodes are handed around by non-const pointers;  most basic nodes are
// immutable, but Nodes that point to other nodes can be mutated by
// replacing nodes; allowed only by the BaseNodeReplacementVisitor.

class Node {
 public:
  virtual ~Node() = default;

  // Poor man's RTTI (also: cheaper). Return non-null if of that type.
  virtual Identifier *CastAsIdentifier() { return nullptr; }
  virtual Assignment *CastAsAssignment() { return nullptr; }
  virtual Scalar *CastAsScalar() { return nullptr; }
  virtual List *CastAsList() { return nullptr; }
  virtual BinOpNode *CastAsBinOp() { return nullptr; }
  virtual FunCall *CastAsFunCall() { return nullptr; }

  virtual void Accept(VoidVisitor *v) = 0;
  virtual Node *Accept(NodeVisitor *v) = 0;
};

// Printing a node. If pointer is non-null, it is dereferenced and printed.
// (defined in print-visitor.*)
std::ostream &operator<<(std::ostream &o, Node *n);
std::string ToString(Node *n);

class Scalar : public Node {
 public:
  enum class ScalarType { kInt, kString };

  virtual ScalarType type() const = 0;

  // Even if this is a number, this will contain the string representation
  // as found in the file (or empty string if Scalar synthesized).
  std::string_view AsString() const { return string_rep_; }
  virtual int64_t AsInt() const { return 0; }

  Scalar *CastAsScalar() final { return this; }

  void Accept(VoidVisitor *v) final;
  Node *Accept(NodeVisitor *v) final;

 protected:
  explicit Scalar(std::string_view value) : string_rep_(value) {}

 private:
  const std::string_view string_rep_;
};

class StringScalar : public Scalar {
 public:
  static StringScalar *FromLiteral(Arena *arena, std::string_view literal);

  // Note: The return value of AsString() has quotes around removed,
  // but potential escaping internally is preserved in this view and points
  // to the original span in the file.
  // Depending on is_raw(), the consumer can make unescape decisions.

  // This is a raw string, i.e. all escape characters shall not be interpreted.
  bool is_raw() const { return is_raw_; }
  bool is_triple_quoted() const { return is_triple_quoted_; }

  ScalarType type() const final { return ScalarType::kString; }

 private:
  friend class Arena;
  StringScalar(std::string_view value, bool is_triple_quoted, bool is_raw)
      : Scalar(value), is_triple_quoted_(is_triple_quoted), is_raw_(is_raw) {}

  const bool is_triple_quoted_;
  const bool is_raw_;
};

class IntScalar : public Scalar {
 public:
  static IntScalar *FromLiteral(Arena *arena, std::string_view literal);

  // AsString() will return the string representation as found in the file.
  int64_t AsInt() const final { return value_; }
  ScalarType type() const final { return ScalarType::kInt; }

 private:
  friend class Arena;
  IntScalar(std::string_view string_rep, int64_t value)
      : Scalar(string_rep), value_(value) {}

  const int64_t value_;
};

class Identifier : public Node {
 public:
  std::string_view id() const { return id_; }

  Identifier *CastAsIdentifier() final { return this; }

  void Accept(VoidVisitor *v) final;
  Node *Accept(NodeVisitor *v) final;

 private:
  friend class Arena;

  // Needs to be owned outside. Typically the region in the
  // original file, that way it allows us report file location.
  explicit Identifier(std::string_view id) : id_(id) {}

  const std::string_view id_;
};

class UnaryExpr : public Node {
 public:
  Node *node() { return node_; }
  TokenType op() const { return op_; }

  void Accept(VoidVisitor *v) final;
  Node *Accept(NodeVisitor *v) final;

 protected:
  friend class Arena;
  friend class BaseNodeReplacementVisitor;
  explicit UnaryExpr(TokenType op, Node *n) : op_(op), node_(n) {}

 private:
  const TokenType op_;
  Node *node_;
};

class BinNode : public Node {
 public:
  Node *left() { return left_; }
  Node *right() { return right_; }

 protected:
  friend class BaseNodeReplacementVisitor;
  BinNode(Node *lhs, Node *rhs) : left_(lhs), right_(rhs) {}

  Node *left_;   // NOLINT(misc-non-private-member-variables-in-classes)
  Node *right_;  // NOLINT(misc-non-private-member-variables-in-classes)
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
  TokenType op() const { return op_; }

  BinOpNode *CastAsBinOp() final { return this; }

  void Accept(VoidVisitor *v) override;
  Node *Accept(NodeVisitor *v) override;

  // Approximate range covered, for file location reporting. Best effort,
  // but should be a valid location and non-empty (except in tests maybe).
  std::string_view source_range() const { return range_; }

 protected:
  BinOpNode(Node *lhs, Node *rhs, TokenType op, std::string_view range)
      : BinNode(lhs, rhs), op_(op), range_(range) {}

 private:
  friend class Arena;

  const TokenType op_;
  const std::string_view range_;  // non-empty if known
};

// List, maps and tuples are all lists.
class List : public Node {
  using Container = ArenaDeque<Node *, 2, 32>;

 public:
  using iterator = Container::iterator;
  enum class Type { kList, kMap, kTuple };

  Type type() const { return type_; }
  size_t size() const { return list_.size(); }
  bool empty() const { return list_.size() == 0; }

  void Append(Arena *arena, Node *value) { list_.Append(value, arena); }

  Node *operator[](size_t pos) const { return list_[pos]; }
  iterator begin() { return list_.begin(); }
  iterator end() { return list_.end(); }

  List *CastAsList() final { return this; }

  void Accept(VoidVisitor *v) final;
  Node *Accept(NodeVisitor *v) final;

 private:
  friend class Arena;
  friend class BaseNodeReplacementVisitor;
  explicit List(Type t) : type_(t) {}

  const Type type_;
  Container list_;
};

// List comprehension for the given type (not only List, but also Map or tuple)
class ListComprehension : public Node {
 public:
  // (FOR subject (IN variable-list-tuple iteratable))
  BinOpNode *for_node() { return for_node_; }
  List::Type type() const { return type_; }

  void Accept(VoidVisitor *v) final;
  Node *Accept(NodeVisitor *v) final;

 private:
  friend class Arena;
  friend class BaseNodeReplacementVisitor;
  ListComprehension(List::Type type, BinOpNode *for_node)
      : type_(type), for_node_(for_node) {}

  const List::Type type_;
  BinOpNode *for_node_;
};

class Ternary : public Node {
 public:
  Node *condition() { return condition_; }
  Node *positive() { return positive_; }
  Node *negative() { return negative_; }

  void Accept(VoidVisitor *v) final;
  Node *Accept(NodeVisitor *v) final;

 private:
  friend class Arena;
  friend class BaseNodeReplacementVisitor;
  Ternary(Node *condition, Node *positive, Node *negative)
      : condition_(condition), positive_(positive), negative_(negative) {}

  Node *condition_;
  Node *positive_;
  Node *negative_;
};

// Simple assignment: the only allowed lvalue is an identifier.
class Assignment : public BinOpNode {
 public:
  // Assignments are typically to some identifier; make this simple to access.
  Identifier *maybe_identifier() {
    if (!left_) return nullptr;
    return left_->CastAsIdentifier();
  }
  Node *value() { return right_; }

  void Accept(VoidVisitor *v) final;
  Node *Accept(NodeVisitor *v) final;

  Assignment *CastAsAssignment() final { return this; }

 private:
  friend class Arena;
  Assignment(Node *lhs, Node *value, std::string_view range)
      : BinOpNode(lhs, value, TokenType::kAssign, range) {}
};

// Function call.
class FunCall : public BinNode {
 public:
  Identifier *identifier() { return static_cast<Identifier *>(left_); }
  List *argument() { return static_cast<List *>(right_); }

  FunCall *CastAsFunCall() final { return this; }

  void Accept(VoidVisitor *v) final;
  Node *Accept(NodeVisitor *v) final;

 private:
  friend class Arena;
  // A function call is essentially an identifier directly followed by a tuple.
  FunCall(Identifier *identifier, List *argument_list)
      : BinNode(identifier, argument_list) {}
};

class VoidVisitor {
 public:
  virtual ~VoidVisitor() = default;
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
  bool WalkNonNull(Node *node) {
    if (node) node->Accept(this);
    return node;
  }
};

// Simple implementation: recursively walk the whole AST.
class BaseVoidVisitor : public VoidVisitor {
 public:
  void VisitAssignment(Assignment *a) override { VisitBinOpNode(a); }
  void VisitFunCall(FunCall *f) override { WalkNonNull(f->right()); }
  void VisitList(List *l) override {
    if (l == nullptr) return;
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

// A visitor that returns a node. Typically to be use to replace a subtree.
class NodeVisitor {
 public:
  virtual ~NodeVisitor() = default;
  virtual Node *VisitAssignment(Assignment *) = 0;
  virtual Node *VisitFunCall(FunCall *) = 0;
  virtual Node *VisitList(List *) = 0;
  virtual Node *VisitBinOpNode(BinOpNode *) = 0;
  virtual Node *VisitUnaryExpr(UnaryExpr *) = 0;
  virtual Node *VisitListComprehension(ListComprehension *) = 0;
  virtual Node *VisitTernary(Ternary *t) = 0;

  virtual Node *VisitScalar(Scalar *) = 0;          // Leaf.
  virtual Node *VisitIdentifier(Identifier *) = 0;  // Leaf.

  // Utility function: if node exists, walk and return valud from visit.
  Node *WalkNonNull(Node *node) { return node ? node->Accept(this) : node; }
};

// Replace nodes with whatever walk on that node yielded.
// Friends with all the owners of nodes, so allowed to assign to private nodes.
// Basis for all kinds of expression eval stuff.
// TODO: given that this is only needed in elaboration, maybe move there.
class BaseNodeReplacementVisitor : public NodeVisitor {
 public:
  Node *VisitAssignment(Assignment *a) override {
    // Not visiting the identifier or tuple of ids; lhs regarded immutable.
    ReplaceWalk(&a->right_);
    return a;
  }

  Node *VisitFunCall(FunCall *f) override {
    ReplaceWalk(&f->left_);
    ReplaceWalk(&f->right_);
    return f;
  }

  Node *VisitList(List *l) override {
    for (Node *&list_element : *l) {
      ReplaceWalk(&list_element);
    }
    return l;
  }

  Node *VisitUnaryExpr(UnaryExpr *e) override {
    ReplaceWalk(&e->node_);
    return e;
  }

  Node *VisitBinOpNode(BinOpNode *b) override {
    ReplaceWalk(&b->left_);
    ReplaceWalk(&b->right_);
    return b;
  }

  Node *VisitListComprehension(ListComprehension *lc) override {
    // Dance around that we actually don't know if a BinOp type comes back.
    if (Node *walk_result = WalkNonNull(lc->for_node_)) {
      if (BinOpNode *binop = walk_result->CastAsBinOp()) {
        lc->for_node_ = binop;
      }
    }
    return lc;
  }

  Node *VisitTernary(Ternary *t) override {
    ReplaceWalk(&t->condition_);
    ReplaceWalk(&t->positive_);
    ReplaceWalk(&t->negative_);
    return t;
  }

  Node *VisitScalar(Scalar *s) override { return s; }
  Node *VisitIdentifier(Identifier *i) override { return i; }

 private:
  void ReplaceWalk(Node **n) { *n = WalkNonNull(*n); }
};

// VoidVisitor Accept()ors
inline void Assignment::Accept(VoidVisitor *v) { v->VisitAssignment(this); }
inline void FunCall::Accept(VoidVisitor *v) { v->VisitFunCall(this); }
inline void List::Accept(VoidVisitor *v) { v->VisitList(this); }
inline void UnaryExpr::Accept(VoidVisitor *v) { v->VisitUnaryExpr(this); }
inline void BinOpNode::Accept(VoidVisitor *v) { v->VisitBinOpNode(this); }
inline void ListComprehension::Accept(VoidVisitor *v) {
  v->VisitListComprehension(this);
}
inline void Ternary::Accept(VoidVisitor *v) { v->VisitTernary(this); }
inline void Scalar::Accept(VoidVisitor *v) { v->VisitScalar(this); }
inline void Identifier::Accept(VoidVisitor *v) { v->VisitIdentifier(this); }

// NodeVisitor Accept()ors
inline Node *Assignment::Accept(NodeVisitor *v) {
  return v->VisitAssignment(this);
}
inline Node *FunCall::Accept(NodeVisitor *v) { return v->VisitFunCall(this); }
inline Node *List::Accept(NodeVisitor *v) { return v->VisitList(this); }
inline Node *UnaryExpr::Accept(NodeVisitor *v) {
  return v->VisitUnaryExpr(this);
}
inline Node *BinOpNode::Accept(NodeVisitor *v) {
  return v->VisitBinOpNode(this);
}
inline Node *ListComprehension::Accept(NodeVisitor *v) {
  return v->VisitListComprehension(this);
}
inline Node *Ternary::Accept(NodeVisitor *v) { return v->VisitTernary(this); }
inline Node *Scalar::Accept(NodeVisitor *v) { return v->VisitScalar(this); }
inline Node *Identifier::Accept(NodeVisitor *v) {
  return v->VisitIdentifier(this);
}

}  // namespace bant
#endif  // BANT_AST_H_
