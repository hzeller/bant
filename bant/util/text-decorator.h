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

#ifndef BANT_TEXT_DECORATOR_H
#define BANT_TEXT_DECORATOR_H

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <ostream>
#include <string_view>
#include <utility>
#include <vector>

namespace bant {
class TextDecorator {
 public:
  using SnippetEmitter = std::function<void(std::ostream &)>;

  // Add a decoration applying start() at offset, and end() and offset + len.
  void AddDecoration(size_t offset, size_t len, SnippetEmitter start,
                     SnippetEmitter end);

  // Emit the text, with decorations at the desired offsets applied.
  void Emit(std::string_view text, std::ostream &out);

 private:
  struct OffsetDecoration {
    // Upper bits: offset; lower bit: start=1 or end=0. That way, when sorting
    // an end of a previous annotation and a start at the same offset of the
    // next annoation are stably sorted. The end is finished before the next
    // start happens.
    OffsetDecoration(size_t offset, bool is_start, SnippetEmitter s)
        : offset_location(offset << 1 | is_start), emitter(std::move(s)) {}
    size_t offset() const { return offset_location >> 1; }

    uint64_t offset_location;
    SnippetEmitter emitter;
  };

  std::vector<OffsetDecoration> decorations_;
};

}  // namespace bant

#endif  // BANT_TEXT_DECORATOR_H
