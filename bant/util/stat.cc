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

#include "bant/util/stat.h"

#include <cstdint>
#include <string>

#include "absl/strings/str_format.h"
#include "absl/time/time.h"

namespace bant {
void Stat::Add(const Stat &other) {
  count += other.count;
  duration += other.duration;
  if (other.bytes_processed.has_value()) {
    AddBytesProcessed(*other.bytes_processed);
  }
}

std::string Stat::ToString() const {
  const int64_t duration_usec = absl::ToInt64Microseconds(duration);
  if (bytes_processed.has_value() && duration_usec > 0) {
    const float megabyte_per_sec = 1.0f * *bytes_processed / duration_usec;
    return absl::StrFormat("%5d %s with %.2f KiB in %8.3fms (%7.2f MB/sec)",
                           count, subject, *bytes_processed / 1024,
                           duration_usec / 1000.0, megabyte_per_sec);
  }
  if (duration_usec > 0) {
    return absl::StrFormat("%5d %s in %.3fms", count, subject,
                           duration_usec / 1000.0);
  }
  return absl::StrFormat("%5d %s", count, subject);
}
}  // namespace bant
