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
#ifndef BANT_SOURCE_FINDER_H
#define BANT_SOURCE_FINDER_H

#include <string_view>
#include <vector>

#include "bant/util/file-utils.h"

namespace bant {
struct PhysicalSourcePath {
  FilesystemPath path;
  bool is_generated;
};

// Given a source file mentioned in the project, return all physical paths this
// can be in.
std::vector<PhysicalSourcePath> PossibleSourceLocations(std::string_view src);

// Given a path to a source file, determine if it looks generated, i.e.
// has one of the telling prefices.
bool LooksLikeGeneratedProjectSource(std::string_view path);
}  // namespace bant

#endif  // BANT_SOURCE_FINDER_H
