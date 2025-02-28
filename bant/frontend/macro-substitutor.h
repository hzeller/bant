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

#ifndef BANT_MACRO_SUBSTITUTOR_H
#define BANT_MACRO_SUBSTITUTOR_H

#include "bant/frontend/ast.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"

namespace bant {
// Replace some known 'special' rules with some macros that expand it to
// genrule()s and cc_library()s so that other commands such as dwyu can reason
// about a bazel project without having to understand the *.bzl files.
//
// For now, however, it requires to hard-code these shallow substitutions;
// these can be found in bant/builtin-macros.bnt
// NB: early stages; this might change substantially over time.
Node *MacroSubstitute(Session &session, ParsedProject *project, Node *ast);
}  // namespace bant
#endif  // BANT_MACRO_SUBSTITUTOR_H
