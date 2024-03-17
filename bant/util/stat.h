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

#ifndef BANT_UTIL_STAT_H
#define BANT_UTIL_STAT_H

#include <cstddef>
#include <optional>
#include <string>

#include "absl/time/time.h"

namespace bant {
struct Stat {
  int count = 0;
  absl::Duration duration;
  std::optional<size_t> bytes_processed;

  // Print readable string with "thing_name" used to describe the count.
  std::string ToString(std::string_view thing_name) const;
};
}  // namespace bant

#endif  // BANT_UTIL_STAT_H
