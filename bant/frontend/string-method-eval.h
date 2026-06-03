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

#ifndef BANT_ELABORATION_STRING_METHOD_EVAL_H
#define BANT_ELABORATION_STRING_METHOD_EVAL_H

#include <string_view>

#include "bant/frontend/ast.h"
#include "bant/frontend/elaboration-factories.h"

namespace bant {
// Method calls on strings, broken out
class StringMethodEval {
 public:
  explicit StringMethodEval(ElaborationFactories &f) : f_(f) {}

  // calls on strings, of the form "foo".method(). Best effort: if not possible
  // don't fail, but return the "orig"inal node.
  Node *StringMethodCall(Node *orig, StringScalar *object, FunCall *method);

 private:
  Node *Format(Node *orig, std::string_view fmt, FunCall *method);
  Node *Join(Node *orig, std::string_view separator, List *args);
  Node *Replace(Node *orig, std::string_view str, List *args);
  Node *StartsWith(Node *orig, std::string_view str, List *args);
  Node *Title(Node *orig, std::string_view str, List *args);
  Node *Rsplit(Node *orig, std::string_view str, List *args);
  Node *Split(Node *orig, std::string_view str, List *args);
  Node *RemovePrefix(Node *orig, StringScalar *object, List *args);
  Node *RemoveSuffix(Node *orig, StringScalar *object, List *args);
  Node *Strip(Node *orig, StringScalar *object, List *args);

  ElaborationFactories &f_;
};
}  // namespace bant

#endif  // BANT_ELABORATION_STRING_EVAL_H
