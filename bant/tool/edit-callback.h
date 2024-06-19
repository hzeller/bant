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

#ifndef BANT_TOOL_EDIT_CALLBACK_
#define BANT_TOOL_EDIT_CALLBACK_

#include <functional>
#include <ostream>
#include <string_view>

#include "bant/types-bazel.h"

namespace bant {

// Edit operations on tagets.
enum class EditRequest {
  kRemove,
  kAdd,
  kRename,
};

// A callback passed to tools that wish to modify BUILD files.
//
// Request kRemove will have "before" set, kAdd "after, and kRename both.
//
// Callers SHOULD have the "before" string-view with its data pointing to
// the original place of the target to be able to extract edit location.
//
// For kRemove and kRename operation, this is simply the original location.
// For kAdd operations, this should be an empty string roughly at the location
// where the addition should take place.
using EditCallback =
  std::function<void(EditRequest op, const BazelTarget &target,
                     std::string_view before, std::string_view after)>;

// Create an EditCallback function that writes "buildozer" dep-edits to "out".
EditCallback CreateBuildozerDepsEditCallback(std::ostream &out);

}  // namespace bant
#endif  // BANT_TOOL_EDIT_CALLBACK_
