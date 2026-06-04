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

#ifndef BANT_PREPROCESS_UTILS_H
#define BANT_PREPROCESS_UTILS_H

#include <string_view>
#include <vector>

#include "bant/frontend/named-content.h"

namespace bant {
// Scan "src" and extract #include project headers (the ones with the quotes
// not angle brackts) from given file. Best effort: may result empty vector.
// Initialize the line index in src to be able to refer back to origainal
// (the index is only updated up to the last relevant output)
// The string_views include the start of the include bracket (so either '<',
// or '"'), but not the end. So it is simple to
// ```
// is_bracket_include = inc[0] == '<'
// inc = inc.substr(1);
// ```
std::vector<std::string_view> ExtractCCIncludes(NamedLineIndexedContent *src);

// Scan a .proto file and extract import paths from "import" statements.
// Returns the import paths (e.g. "foo/bar.proto") from lines like:
//   import "foo/bar.proto";
std::vector<std::string_view> ExtractProtoImports(NamedLineIndexedContent *src);

}  // namespace bant

#endif  // BANT_PREPROCESS_UTILS_H
