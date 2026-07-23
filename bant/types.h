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

#ifndef BANT_TYPES_
#define BANT_TYPES_

#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"

// Some convenient types to emphasize the relationship rather than the
// implementation. The default versions with short names are ordered, which
// allows some stable outputs on printing.
// TODO: this should also be encoded in some type-traits, so that we can
// create compilation issues for anything that iterates and prints.

// A 1:1 relationship with a sorted iteration order.
template <typename K, typename V>
using OneToOne = absl::btree_map<K, V>;

// A 1:1 optimized for Look-up, but random iteration order
template <typename K, typename V>
using OneToOneForLookup = absl::flat_hash_map<K, V>;

template <typename K, typename V>
using OneToNSet = absl::btree_map<K, absl::btree_set<V>>;

template <typename K, typename V>
using OneToN = absl::btree_map<K, std::vector<V>>;

#endif  // BANT_TYPES_
