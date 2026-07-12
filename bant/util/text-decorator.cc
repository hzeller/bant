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

#include "bant/util/text-decorator.h"

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <string_view>
#include <utility>

namespace bant {

void TextDecorator::AddDecoration(size_t offset, size_t len,
                                  SnippetEmitter start, SnippetEmitter end) {
  if (start) decorations_.emplace_back(offset, true, std::move(start));
  if (end) decorations_.emplace_back(offset + len, false, std::move(end));
}

// Emit the text, but with decorations applied.
void TextDecorator::Emit(std::string_view text, std::ostream &out) {
  std::stable_sort(decorations_.begin(), decorations_.end(),
                   [](const OffsetDecoration &a, const OffsetDecoration &b) {
                     return a.offset_location < b.offset_location;
                   });

  size_t last_offset = 0;
  for (const OffsetDecoration &decoration : decorations_) {
    const size_t new_offset = decoration.offset();
    out << text.substr(last_offset, new_offset - last_offset);
    decoration.emitter(out);
    last_offset = new_offset;
  }
  out << text.substr(last_offset);
}

}  // namespace bant
