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
// Simplified preprocessing: Classify ranges in the source file if included
// or excluded due to #ifdefs.
// Given a CC source code and some known defines, return all the ranges
// with classification bit.
// The returned string views point to the original source
// and are returned in order.
// For now, only existence define (true/false), no expressions, are evaluated.
//
// The "define_values" map should contain pre-existing macros at call time
// and will be updated if defines/undefs are processed inside the source.
struct TaggedRange {
  std::string_view range;  // Affected range in the source file.
  bool is_included;        // if this is included or #ifdef'ed out.
  bool operator==(const TaggedRange &) const = default;
};
using DefineMap = OneToOne<std::string_view, bool>;
std::vector<TaggedRange> ExtractActiveCCIfdefRanges(std::string_view source,
                                                    DefineMap &define_values);

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
