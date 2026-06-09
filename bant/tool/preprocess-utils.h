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

#include "bant/explore/query-utils.h"
#include "bant/frontend/named-content.h"
#include "bant/types.h"

namespace bant {
using DefineMap = OneToOne<std::string_view, bool>;
// Given a target, look at copt =[] and define = [] and return. To be used
// as input to ExtractActiveCCIfdefRanges()
DefineMap GetDefinesFromTarget(const query::Result &target);

// Scan "src" and extract #include project headers.
// Returns a vector of TaggedIncludes that contain the include name and
// if it was included with angled bracket and if it is excluded due to
// preprocessing.
struct TaggedInclude {
  std::string_view include;  // path of the include file.
  bool is_angle_bracket;     // if true, included via <>, otherwise ""
  bool is_ifdefed_out;       // if not considered due to macros in defines.
  bool operator==(const TaggedInclude &) const = default;
};
std::vector<TaggedInclude> ExtractCCIncludes(NamedLineIndexedContent *src,
                                             const DefineMap &defines);

// Scan a .proto file and extract import paths from "import" statements.
// Returns the import paths (e.g. "foo/bar.proto") from lines like:
//   import "foo/bar.proto";
std::vector<std::string_view> ExtractProtoImports(NamedLineIndexedContent *src);

}  // namespace bant

#endif  // BANT_PREPROCESS_UTILS_H
