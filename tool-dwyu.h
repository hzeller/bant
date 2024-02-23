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

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "project-parser.h"

namespace bant {

// Extract #include project headers (the ones with the quotes not angle
// brackts) from given file. Best effort: may result empty vector.
std::vector<std::string> ExtractCCIncludes(std::string_view content);

// Look through the sources mentioned in the file
void PrintDependencyEdits(const ParsedProject &project, std::ostream &out,
                          std::ostream &info_out);

}  // namespace bant

#endif  // BANT_TOOL_DWYU_
