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

#include "bant/tool/edit-callback.h"

#include <ostream>
#include <string_view>

#include "bant/types-bazel.h"

namespace bant {
EditCallback CreateBuildozerDepsEditCallback(std::ostream &out) {
  return [&out](EditRequest edit, const BazelTarget &target,
                std::string_view before, std::string_view after) {
    switch (edit) {
    case EditRequest::kRemove:
      out << "buildozer 'remove deps " << before << "' " << target << "\n";
      break;
    case EditRequest::kAdd:
      out << "buildozer 'add deps " << after << "' " << target << "\n";
      break;
    case EditRequest::kRename:
      out << "buildozer 'replace deps " << before << " " << after << "' "
          << target << "\n";
      break;
    }
  };
}

}  // namespace bant
