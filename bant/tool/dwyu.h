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

#ifndef BANT_TOOL_DWYU_
#define BANT_TOOL_DWYU_

#include <cstddef>
#include <string_view>
#include <vector>

#include "bant/frontend/named-content.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/tool/edit-callback.h"
#include "bant/types-bazel.h"

namespace bant {

// Scan "src" and extract #include project headers (the ones with the quotes
// not angle brackts) from given file. Best effort: may result empty vector.
// Initialize the line index in src to be able to refer back to origainal.
std::vector<std::string_view> ExtractCCIncludes(NamedLineIndexedContent *src);

// Look through the sources mentioned in the file, check what they include
// and determine what dependencies need to be added/removed.
// Input should be an elaborated project for best availability of inspected
// lists.
// Return number of edits that have been emitted.
size_t CreateDependencyEdits(Session &session, const ParsedProject &project,
                             const BazelTargetMatcher &pattern,
                             const EditCallback &emit_deps_edit);

}  // namespace bant

#endif  // BANT_TOOL_DWYU_
