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

#ifndef BANT_ARENA_H_
#define BANT_ARENA_H_

#include <algorithm>
#include <cstddef>
#include <deque>
#include <iostream>
#include <memory>
#include <utility>

namespace bant {
// Arena: Provide allocation of memory that can be deallocated at once.
// Fast, but does not call any destructors so content better be PODs.
class Arena {
 public:
  explicit Arena(int block_size) : block_size_(block_size) {}
  Arena(Arena &&) noexcept = default;
  Arena(const Arena &) = delete;

  void *Alloc(size_t size) {
    if (pos_ == nullptr || size > (size_t)(end_ - pos_)) {
      NewBlock(std::max(size, block_size_));  // max: allow oversized allocs
    }
    total_allocations_++;
    total_bytes_ += size;
    char *start = pos_;
    pos_ += size;
    return start;
  }

  // Convenience allocation calling T constructor in place
  template <typename T, class... U>
  T *New(U &&...args) {
    return new (Alloc(sizeof(T))) T(std::forward<U>(args)...);
  }

  ~Arena() {
    if (verbose_) {
      std::cerr << "Arena: " << total_allocations_ << " allocations "
                << "in " << blocks_.size() << " blocks; " << total_bytes_ / 1e6
                << " MB.\n";
    }
  }

  void SetVerbose(bool verbose) { verbose_ = verbose; }

 private:
  // Allocate new block, updates current block.
  void NewBlock(size_t request) {
    char *buffer = blocks_.emplace_back(new char[request]).get();
    end_ = buffer + request;
    pos_ = buffer;
  }

  const size_t block_size_;
  std::deque<std::unique_ptr<char[]>> blocks_;

  const char *end_ = nullptr;
  char *pos_ = nullptr;

  bool verbose_ = false;
  size_t total_bytes_ = 0;
  size_t total_allocations_ = 0;
};
}  // namespace bant
#endif  // BANT_ARENA_H_
