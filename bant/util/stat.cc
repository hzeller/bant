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

std::string Stat::ToString(bool with_highlight) const {
  const int64_t duration_usec = absl::ToInt64Microseconds(duration);
  static constexpr int kSubjectWidth = 17;
  static constexpr float kMiB = 1 << 20;
  const char *mark = with_highlight ? "\033[1m" : "";
  const char *reset = with_highlight ? "\033[0m" : "";
  if (bytes_processed.has_value() && duration_usec > 0) {
    const float megabyte_per_sec = 1e6f * *bytes_processed / kMiB / duration_usec;
    return absl::StrFormat("%s%6d%s %-*s in %s%8.3fms%s (%7.1f KiB; %s%7.2f MiB/sec%s)",
                           mark, count, reset,
                           kSubjectWidth, subject,
                           mark, duration_usec / 1000.0, reset,
                           *bytes_processed / 1024,
                           mark, megabyte_per_sec, reset);
  }
  if (duration_usec > 0) {
    return absl::StrFormat("%s%6d%s %-*s in %s%8.3fms%s",
                           mark, count, reset, kSubjectWidth, subject,
                           mark, duration_usec / 1000.0, reset);
  }
  return absl::StrFormat("%s%6d%s %s", mark, count, reset, subject);
}
}  // namespace bant
