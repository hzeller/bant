/* -*- c++ -*- */
#ifndef BANT_ARENA_H
#define BANT_ARENA_H

#include <algorithm>
#include <cstddef>
#include <deque>
#include <iostream>
#include <memory>
#include <utility>

// Arena: Provide allocation of memory that can be deallocated at once.
// Fast, but does not call any destructors of the objects contained.
class Arena {
 public:
  explicit Arena(int block_size) : block_size_(block_size) {}

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
    std::cerr << "Arena: " << total_allocations_ << " allocations "
              << "in " << blocks_.size() << " blocks; " << total_bytes_
              << " bytes.\n";
  }

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

  size_t total_bytes_ = 0;
  size_t total_allocations_ = 0;
};

#endif  // BANT_ARENA_H
