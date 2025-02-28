// bant - Bazel Navigation Tool
// Copyright (C) 2025 Henner Zeller <h.zeller@acm.org>
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

#ifndef BANT_SUBSTITUE_COPY_H
#define BANT_SUBSTITUE_COPY_H

#include "bant/explore/query-utils.h"
#include "bant/frontend/ast.h"
#include "bant/util/arena.h"

namespace bant {
// Given an immutable tree "ast", and a map that contains identifiers to node
// mapping, create a copy of the ast and substitue all identifiers with values
// provided in "varmap".
//
// Unlike the elaboration, this does _not_ modify the original AST, but
// copies if needed. This is a copy-on-write operation, so only nodes that
// depend on a variable substitution will be newly allocated in the "arena"
// while unaffected nodes will be hocked up as they are.
//
// In consequence, iff there are no variable subsitutions, the returned Node
// pointer equals the input "ast".
//
// Caller needs to make sure to not provide variable subsitutions that reach
// into the original AST to not accidentally create cycles (unlikely scenario).
Node *VariableSubstituteCopy(Node *ast, Arena *arena,
                             const query::KwMap &varmap);
}  // namespace bant

#endif  // BANT_SUBSTITUE_COPY_H
