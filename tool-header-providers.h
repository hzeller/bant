// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#ifndef BANT_TOOL_HEADER_PROVIDER_
#define BANT_TOOL_HEADER_PROVIDER_

#include <stdio.h>

#include <map>
#include <string>

#include "project-parser.h"

namespace bant {
using HeaderToTargetMap = std::map<std::string, std::string>;
HeaderToTargetMap ExtractHeaderToLibMapping(const ParsedProject &project);

void PrintLibraryHeaders(FILE *out, const ParsedProject &project);
}  // namespace bant

#endif  // BANT_TOOL_HEADER_PROVIDER_
