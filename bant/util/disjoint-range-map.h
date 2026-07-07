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
#ifndef BANT_DISJOINT_RANGE_MAP_H
#define BANT_DISJOINT_RANGE_MAP_H

#include <utility>

#include "absl/container/btree_map.h"

namespace bant {
// Mapping non-overlapping ranges to values and providing way to look up
// by any subrange.
//
// Typical use-case is to map sub-ranges of std::string_views to locators.
// Range types need to have a begin(), end() iterator (which must not be
// changing while stored in this map). [comparable]
template <typename KeyRange, typename ValueType>
class DisjointRangeMap {
  using Container =
    absl::btree_map<typename KeyRange::const_iterator,
                    std::pair<typename KeyRange::const_iterator, ValueType>>;

 public:
  bool Insert(const KeyRange &key, ValueType v) {
    // No error handling, such as subrange overlap test.
    return container_.insert({key.end(), {key.begin(), std::move(v)}}).second;
  }

  // Behave a little bit like a container from the users' perpective, at
  // least for 'find' purposes. FindBysubrange() return an iterator that can
  // be compared to end() and dereference to a Value. This wrapper class
  // around the pointer ensures the limited functionality.
  class const_iterator {
   public:
    const ValueType &operator*() const { return *it_; }
    const ValueType &operator->() const { return *it_; }
    bool operator==(const const_iterator &o) const { return it_ == o.it_; }
    bool operator!=(const const_iterator &o) const { return it_ != o.it_; }

   private:
    friend class DisjointRangeMap;
    explicit const_iterator(const ValueType *v) : it_(v) {}
    const ValueType *it_;
  };

  const_iterator end() const { return const_iterator(nullptr); }

  // Find value by subrange or end() if it doesn't exist.
  const_iterator FindBySubrange(const KeyRange &subrange) const {
    const auto &lower = container_.lower_bound(subrange.end());
    if (lower == container_.end()) return end();
    const auto &mapped_to = lower->second;
    if (mapped_to.first > subrange.begin()) return end();
    return const_iterator(&mapped_to.second);
  }

 private:
  Container container_;
};
}  // namespace bant
#endif  // BANT_DISJOINT_RANGE_MAP_H
