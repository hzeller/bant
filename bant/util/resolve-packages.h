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

#ifndef BANT_UTIL_RESOLVE_PACKAGES_
#define BANT_UTIL_RESOLVE_PACKAGES_

#include <ostream>

#include "bant/frontend/project-parser.h"
#include "bant/types-bazel.h"

namespace bant {
// Given the current project, and the desired bazel rule pattern, resolve
// all the relevant dependencies recusively until all dependencies are
// resolved or could not be parsed.
// TODO: given a pattern, we might be able to narrow.
void ResolveMissingDependencies(ParsedProject *project,
                                const BazelPattern &pattern,
                                bool verbose,
                                std::ostream &info_out, std::ostream &err_out);
}  // namespace bant

#endif  // BANT_UTIL_RESOLVE_PACKAGES_
