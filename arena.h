/* -*- c++ -*- */
#ifndef ARENA_H
#define ARENA_H

#include <cstddef>
#include <iostream>
#include <utility>

// Arena: Provide allocation of memory that can be deallocated at once.
// Fast, but does not call any destructors.
class Arena {
 public:
  Arena(int max_size)
      : buffer_(new char[max_size]), end_(buffer_ + max_size), pos_(buffer_) {}

  char *Alloc(size_t size) {
    total_allocations_++;
    char *start = pos_;
    if (pos_ + size > end_) {
      // TODO: just add more blocks.
      std::cerr << "Allocation exhausted. New block alloc not implmented yet\n";
      return nullptr;
    }
    pos_ += size;
    return start;
  }

  // Convenience constructor in place
  template <typename T, class... U>
  T *New(U &&...args) {
    const size_t size = sizeof(T);
    return new (Alloc(size)) T(std::forward<U>(args)...);
  }

  ~Arena() {
    std::cerr << "Arena: " << total_allocations_ << " allocations with "
              << (pos_ - buffer_) << " bytes.\n";
    delete[] buffer_;
  }

 private:
  char *const buffer_;
  const char *const end_;
  char *pos_;
  size_t total_allocations_ = 0;
};

#endif  // ARENA_H
