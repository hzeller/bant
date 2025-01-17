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

#include "bant/frontend/named-content.h"

#include <cstdlib>
#include <string_view>

#include "absl/log/check.h"
#include "bant/frontend/linecolumn-map.h"
#include "bant/frontend/source-locator.h"

namespace bant {
FileLocation NamedLineIndexedContent::GetLocation(std::string_view text) const {
  CHECK(text.begin() >= content().begin() && text.end() <= content().end())
    << "Attempt to pass '" << text << "' which is not within " << name_;
  return {name_, line_index_.GetRange(text)};
}

std::string_view NamedLineIndexedContent::GetSurroundingLine(
  std::string_view text) const {
  CHECK(text.begin() >= content().begin() && text.end() <= content().end())
    << "Attempt to pass '" << text << "' which is not within " << name_;

  const char *start = text.data();
  while (start > content_.data() && *(start - 1) != '\n') {
    --start;
  }

  const char *end = text.data() + text.size();
  const char *const end_of_content = content_.data() + content_.size();
  while (end < end_of_content && *end != '\n') {
    ++end;
  }

  return {start, static_cast<size_t>(end - start)};
}
}  // namespace bant
