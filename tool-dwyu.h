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

#ifndef BANT_TOOL_DWYU_
#define BANT_TOOL_DWYU_

#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "project-parser.h"
#include "types-bazel.h"

namespace bant {

// Extract #include project headers (the ones with the quotes not angle
// brackts) from given file. Best effort: may result empty vector.
std::vector<std::string> ExtractCCIncludes(std::string_view content);

// Edit operations on tagets.
enum class EditRequest {
  kRemove,
  kAdd,
  kRename,
};

// Request kRemove will have "before" set, kAdd "after, and kRename both.
using EditCallback =
  std::function<void(EditRequest, const BazelTarget &target,
                     std::string_view before, std::string_view after)>;

// Look through the sources mentioned in the file, check what they include
// and determine what dependencies need to be added/remove.
// Also, if there are some simple corrections that can be made emit these.
void CreateDependencyEdits(const ParsedProject &project, Stat &stats,
                           std::ostream &info_out,
                           const EditCallback &emit_deps_edit);

// Create an EditCallback function that writes "buildozer" edits to out.
EditCallback CreateBuildozerPrinter(std::ostream &out);

}  // namespace bant

#endif  // BANT_TOOL_DWYU_
