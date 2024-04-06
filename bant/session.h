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

#ifndef BANT_SESSION_H
#define BANT_SESSION_H

#include <map>
#include <ostream>
#include <string_view>

#include "bant/output-format.h"
#include "bant/util/stat.h"

namespace bant {
// A session contains some global settings such as output/verbose requests
// as well as access to streams for general output or error and info messages.
class Session {
 public:
  using StatMap = std::map<std::string_view, bant::Stat>;

  Session(std::ostream *out, std::ostream *info, bool verbose, OutputFormat fmt)
      : out_(out), info_(info), verbose_(verbose), output_format_(fmt) {}

  std::ostream &out() { return *out_; }
  std::ostream &info() { return *info_; }
  std::ostream &error() { return *info_; }

  bool verbose() const { return verbose_; }
  OutputFormat output_format() const { return output_format_; }

  // Get a stat object to fill/update. The "subsystem_name" describes who is
  // collecting stats, the "subject" is what (e.g. file-count etc).
  // Both strings needs to outlive this session object, so typically a regular
  // compile-time string constant.
  Stat &GetStatsFor(std::string_view subsystem_name, std::string_view subject) {
    auto inserted = stats_.insert({subsystem_name, {subject}});
    if (inserted.second) {
      stat_init_key_order_.push_back(subsystem_name);
    }
    return inserted.first->second;
  }

  // Return stat keys in the sequence they have been added.
  const std::vector<std::string_view> &stat_keys() const {
    return stat_init_key_order_;
  }

  // Get stat for subsystem or nullptr, if there is no such stat.
  const Stat *stat(std::string_view subsystem_name) const {
    auto found = stats_.find(subsystem_name);
    return (found != stats_.end()) ? &found->second : nullptr;
  }

 private:
  StatMap stats_;
  std::vector<std::string_view> stat_init_key_order_;

  std::ostream *out_;
  std::ostream *info_;
  bool verbose_;
  OutputFormat output_format_;
};
}  // namespace bant
#endif  // BANT_SESSION_H
