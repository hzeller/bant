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
#include <string>

#include "bant/util/stat.h"

namespace bant {
// A session allows all tools and functios to know where to write
// error and info messages.
class Session {
 public:
  // TODO: this should be something more akin of a linked hash map get
  // a chronology what happend when.
  using StatMap = std::map<std::string, bant::Stat>;

  Session(std::ostream *out, std::ostream *info, bool verbose)
      : out_(out), info_(info), verbose_(verbose) {}

  std::ostream &out() { return *out_; }
  std::ostream &info() { return *info_; }
  std::ostream &error() { return *info_; }

  bool verbose() const { return verbose_; }

  // Get a stat object to fill/update. Various subsystems can request these.
  Stat &GetStatsFor(const std::string &subsystem_name) {
    return stats_.insert({subsystem_name, {}}).first->second;
  }

  const StatMap &stats() const { return stats_; }

 private:
  StatMap stats_;
  std::ostream *out_;
  std::ostream *info_;
  bool verbose_;
};
}  // namespace bant
#endif  // BANT_SESSION_H
