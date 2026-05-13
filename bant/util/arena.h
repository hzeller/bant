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

#ifndef BANT_ARENA_H_
#define BANT_ARENA_H_

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <utility>

namespace bant {
// Arena: Provide allocation of memory that can be deallocated at once.
// Fast, but does not call any destructors so content better be PODs.
class Arena {
  static constexpr int kAlignment = 8;

  // With kDoBulkAllocations
  //   - true:  do a classical 'arena' allocation with large consecutive
  //            blocks with small elements inside.
  //   - false: Do one malloc() per request allocation. This is slightly
  //            slower, but allows to meaningfully use address sanitizer.
  static constexpr bool kDoBulkAllocations =
#ifdef ADDRESS_SANITIZER
    false;
#else
    true;
#endif

 public:
  explicit Arena(int block_size) : block_size_(block_size) {}
  Arena(Arena &&) noexcept = default;
  Arena(const Arena &) = delete;

  void *Alloc(size_t size) {
    // Round up size to next alignment value
    // TODO: instead of a fixed alignment, take alignof() of type to allocate
    // into account.
    size += kAlignment - (size % kAlignment);
    total_allocations_++;
    total_bytes_ += size;

    return kDoBulkAllocations ? NextBlockAlloc(size) : LowLevelAlloc(size);
  }

  // Convenience allocation calling T constructor in place
  template <typename T, class... U>
  [[nodiscard]] T *New(U &&...args) {
    return new (Alloc(sizeof(T))) T(std::forward<U>(args)...);
  }

  ~Arena() {
    for (void *p : blocks_) {
      std::free(p);
    }

    if (!verbose_ || blocks_.empty()) return;

    fprintf(stderr, "Arena: %6zu allocations in %zu blocks; ",
            total_allocations_, blocks_.size());
    if (total_bytes_ > 10L * 1024) {
      fprintf(stderr, "%6.3f MiB.\n", total_bytes_ / (1024.0 * 1024.0));
    } else {
      fprintf(stderr, "%4zu Bytes.\n", total_bytes_);
    }
  }

  void SetVerbose(bool verbose) { verbose_ = verbose; }

 private:
  void *LowLevelAlloc(size_t size) {
    return blocks_.emplace_back(std::aligned_alloc(kAlignment, size));
  }

  void *NextBlockAlloc(size_t size) {
    if (pos_ == nullptr || std::cmp_greater(size, (end_ - pos_))) {
      NewBlock(std::max(size, block_size_));  // max: allow oversized allocs
    }
    void *const start = pos_;
    pos_ += size;
    return start;
  }

  // Allocate new block, updates current block.
  void NewBlock(size_t request) {
    char *buffer = static_cast<char *>(LowLevelAlloc(request));
    end_ = buffer + request;
    pos_ = buffer;
  }

  const size_t block_size_;
  std::deque<void *> blocks_;

  const char *end_ = nullptr;
  char *pos_ = nullptr;

  bool verbose_ = false;
  size_t total_bytes_ = 0;
  size_t total_allocations_ = 0;
};
}  // namespace bant
#endif  // BANT_ARENA_H_
