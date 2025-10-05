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

#include "bant/tool/edit-callback.h"

#include <ostream>
#include <sstream>
#include <string_view>

#include "bant/types-bazel.h"
#include "bant/util/grep-highlighter.h"

namespace bant {
EditCallback CreateBuildozerDepsEditCallback(std::ostream &out,
                                             const GrepHighlighter &grepper) {
  return [&out, &grepper](EditRequest edit, const BazelTarget &target,
                          std::string_view before, std::string_view after) {
    std::stringstream tmp_out;
    switch (edit) {
    case EditRequest::kRemove:
      tmp_out << "'remove deps " << before << "' " << target;
      break;
    case EditRequest::kAdd:
      tmp_out << "'add deps " << after << "' " << target;
      break;
    case EditRequest::kRename:
      tmp_out << "'replace deps " << before << " " << after << "' " << target;
      break;
    }
    grepper.EmitMatch(tmp_out.str(), out, "buildozer ", "\n");
  };
}

}  // namespace bant
