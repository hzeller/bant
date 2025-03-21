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

#ifndef BANT_UTIL_STAT_H
#define BANT_UTIL_STAT_H

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace bant {

// Add time encountered in the scope to duration.
class ScopedTimer {
 public:
  explicit ScopedTimer(absl::Duration *to_update)
      : to_update_(to_update), start_(absl::Now()) {}
  ~ScopedTimer() { *to_update_ += absl::Now() - start_; }

 private:
  absl::Duration *const to_update_;
  const absl::Time start_;
};

struct Stat {
  explicit Stat(std::string_view subject) : subject(subject) {}

  // Stat constructor without parameter should only be used for intermediate
  // stats to be Add()-ed later.
  Stat() : Stat("no-stat-subject") {}
  const std::string_view subject;  // Descriptive name this stat is counting.

  int count = 0;
  absl::Duration duration;
  std::optional<size_t> bytes_processed;

  // Add processed bytes, implicitly un-optionaling bytes_processed.
  void AddBytesProcessed(size_t byte_count) {
    if (bytes_processed.has_value()) {
      bytes_processed = *bytes_processed + byte_count;
    } else {
      bytes_processed = byte_count;
    }
  }

  // Add a stat collected separately.
  void Add(const Stat &other);

  // Print readable string with "subject" used to describe the count.
  std::string ToString() const;
};

inline std::ostream &operator<<(std::ostream &out, const Stat &stat) {
  out << stat.ToString();
  return out;
}
}  // namespace bant

#endif  // BANT_UTIL_STAT_H
