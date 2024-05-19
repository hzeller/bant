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

#include "bant/frontend/named-content.h"

#include <string_view>

#include "absl/log/check.h"
#include "bant/frontend/linecolumn-map.h"
#include "bant/frontend/source-locator.h"

namespace bant {
LineColumnRange NamedLineIndexedContent::GetLocation(std::string_view text) const {
  CHECK(text.begin() >= content().begin() && text.end() <= content().end())
    << "Attempt to pass '" << text << "' which is not within " << source_name();
  return line_index_.GetRange(text);
}

}  // namespace bant
