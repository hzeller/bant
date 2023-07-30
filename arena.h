/* -*- c++ -*- */
#ifndef ARENA_H
#define ARENA_H

#include <cstddef>
#include <iostream>

// TODO: this is a sketch only, does not actually implement the arena aspect
// and will leak memory.

// Arena: Provide allocation of memory that can be deallocated at once.
// Fast, but does not call any destructors.
class Arena {
 public:
  // TODO: provid template that takes type and forwards constructor calls.

  char *Alloc(size_t size) {
    total_allocations_++;
    total_memory_ += size;
    return new char[size];  // TODO: actuallly make this an arena.
  }

  ~Arena() {
    std::cerr << "Arena: " << total_allocations_ << " allocations with "
              << total_memory_ << " bytes.\n";
  }

 private:
  size_t total_allocations_ = 0;
  size_t total_memory_ = 0;
};

#endif  // ARENA_H
