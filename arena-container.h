// -*- c++ -*-
// Containers that can store in an arena.

#include <cassert>

#include "arena.h"

// Expected to be allocated in itself in an Arena.
//
// This allows to append new values and iterate through them. No removal
// possible. Random access provided, but will behave O(N).
// With SIZE==1, this is the degenerate case of a simple linked list.
// TODO: have a MIN_BLOCK size that is doubles until MAX_BLOCK size reached.
template <typename T, int SIZE = 3>
class ArenaDeque {
 private:
  struct Block {
    Block *next = nullptr;
    T value[SIZE];
  };

 public:
  ArenaDeque() : current_(&top_) {}

  T &Append(T value, Arena *arena) {
    if (next_block_pos_ >= SIZE) {
      current_->next = arena->New<Block>();
      current_ = current_->next;
      next_block_pos_ = 0;
    }
    T &location = current_->value[next_block_pos_];
    ++next_block_pos_;
    location = value;
    return location;
  }

  // Slow: first ccouple of SIZE values are O(1), but further down O(N)
  const T &operator[](size_t pos) const {
    const Block *access_block = &top_;
    while (pos >= SIZE) {
      access_block = access_block->next;
      pos -= SIZE;
    }
    return access_block->value[pos];
  }

  class const_iterator {
   public:
    const T &operator*() {
      assert(block_ != nullptr);
      return block_->value[pos_];
    }
    const_iterator &operator++() {
      ++pos_;
      if (pos_ >= SIZE) {
        block_ = block_->next;
        pos_ = 0;
      }
      return *this;
    }
    bool operator==(const const_iterator &other) const {
      return other.block_ == block_ && other.pos_ == pos_;
    }
    bool operator!=(const const_iterator &other) const {
      return !(*this == other);
    }

   private:
    friend class ArenaDeque;
    const_iterator(const Block *block, size_t pos) : block_(block), pos_(pos) {}
    const Block *block_;
    size_t pos_;
  };

  const_iterator begin() const { return const_iterator(&top_, 0); }
  const_iterator end() const {
    return next_block_pos_ == SIZE ? const_iterator(nullptr, 0)
                                   : const_iterator(current_, next_block_pos_);
  }

 private:
  Block top_;  // first block allocated with class
  Block *current_;
  size_t next_block_pos_ = 0;
};
