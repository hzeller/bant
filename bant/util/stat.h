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

#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace bant {

class Stat {
 public:
  explicit Stat(std::string_view subject) : subject_(subject) {}

  // Stat constructor without parameter should only be used for intermediate
  // stats to be Add()-ed later.
  Stat() : Stat("no-stat-subject") {}

  // Add processed bytes, implicitly un-optionaling bytes_processed.
  void AddBytesProcessed(size_t byte_count) {
    if (bytes_processed_.has_value()) {
      bytes_processed_ = *bytes_processed_ + byte_count;
    } else {
      bytes_processed_ = byte_count;
    }
  }

  void AddCount(int c) {
    const absl::MutexLock l(mu_);
    count_ += c;
  }
  void IncCount() { AddCount(1); }
  int count() const {
    const absl::MutexLock l(mu_);
    return count_;
  }

  void AddDuration(const absl::Duration &d) {
    const absl::MutexLock l(mu_);
    duration_ += d;
  }
  absl::Duration duration() const {
    const absl::MutexLock l(mu_);
    return duration_;
  }

  // Add a stat collected separately.
  void Add(const Stat &other);

  // Print readable string with "subject" used to describe the count.
  std::string ToString(bool with_hightlight = true) const;

 private:
  const std::string_view subject_;  // Descriptive name this stat is counting.

  mutable absl::Mutex mu_;
  int count_ = 0;
  absl::Duration duration_;
  std::optional<size_t> bytes_processed_;
};

// Add time encountered in the scope to duration.
class ScopedTimer {
 public:
  explicit ScopedTimer(Stat &to_update)
      : to_update_(to_update), start_(absl::Now()) {}

  ~ScopedTimer() { to_update_.AddDuration(absl::Now() - start_); }

 private:
  Stat &to_update_;
  const absl::Time start_;
};

inline std::ostream &operator<<(std::ostream &out, const Stat &stat) {
  out << stat.ToString();
  return out;
}
}  // namespace bant

#endif  // BANT_UTIL_STAT_H
