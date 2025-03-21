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

#ifndef BANT_TOOL_COMPILATiON_DB_
#define BANT_TOOL_COMPILATiON_DB_

#include <string>
#include <string_view>
#include <vector>

#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/types-bazel.h"

namespace bant {
// Given a bazelrc content, extract all the cxx options relevant for buildling.
std::vector<std::string> ExtractOptionsFromBazelrc(std::string_view content);

// Create compilation_flags.txt or compilation DB compatible with clang tools
// such as clang-tidy or clangd. If "as_compilation_db" is on, emit as
// json compilation database, otherwise as simple compile flags.
//
// Requires a fully alaborated "project".
void WriteCompilationFlags(Session &session, const BazelTargetMatcher &pattern,
                           ParsedProject *project, bool as_compilation_db);
}  // namespace bant
#endif  // BANT_TOOL_COMPILATiON_DB_
