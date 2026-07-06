// bant - Bazel Navigation Tool
// Copyright (C) 2026 Henner Zeller <h.zeller@acm.org>
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
#ifndef BANT_CROSS_REFERENCE_H
#define BANT_CROSS_REFERENCE_H

#include <string>
#include <string_view>
#include <variant>

#include "bant/frontend/parsed-project.h"
#include "bant/frontend/source-locator.h"
#include "bant/util/disjoint-range-map.h"

namespace bant {
struct FileNameReference {
  // A filename, relative to project root.
  std::string filename;
};

using CrossReference = std::variant<FileLocation, FileNameReference>;

using CrossReferenceMap =
  DisjointRangeMap<std::string_view, bant::CrossReference>;

// Given a project, build cross references, that match various strings to
// places that can be shown in e.g. hyperlinks.
CrossReferenceMap BuildCrossReferences(const ParsedProject &project);
}  // namespace bant

#endif  // BANT_CROSS_REFERENCE_H
