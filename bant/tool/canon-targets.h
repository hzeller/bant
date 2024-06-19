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

#ifndef BANT_TOOL_CANON_TARGETS_
#define BANT_TOOL_CANON_TARGETS_

#include <cstdlib>

#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/tool/edit-callback.h"
#include "bant/types-bazel.h"

namespace bant {
// Fix dep targets that can be canonicalized
//  * `//foo/bar:baz` when already in `//foo/bar` becomes `:baz`
//  * `//foo:foo` becomes `//foo`
//  * `@foo//:foo` becomes `@foo`
//  * `foo` without `:` prefix becomes `:foo`
// Returns number of edits emitted.
size_t CreateCanonicalizeEdits(Session &session, const ParsedProject &project,
                               const BazelPattern &pattern,
                               const EditCallback &emit_canon_edit);
}  // namespace bant
#endif  // BANT_TOOL_CANON_TARGETS_
