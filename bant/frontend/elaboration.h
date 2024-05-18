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

// Expression evaluation for some const-evaluatable things that are useful
// for later stages, like expanding variables, list concatenations or
// glob() calls. Not fully fledged evaluation, let's call it elaboration.
#ifndef BANT_ELABORATION_H
#define BANT_ELABORATION_H

#include "bant/frontend/ast.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"

namespace bant {

// Elaborate given AST in the context of the parsed project. The project
// supplies the arena to allocate possibly new nodes and to provide
// SourceLocator services to query and register.
//
// Returns (possibly modified) AST.
Node *Elaborate(ParsedProject *project, Node *ast);

// Elaborate all files in the given project.
void Elaborate(Session &session, ParsedProject *project);
}  // namespace bant

#endif  // BANT_ELABORATION_H
