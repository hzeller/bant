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

#include <memory>
#include <ostream>
#include <string_view>

#include "bant/output-format.h"
#include "bant/types.h"
#include "bant/util/stat.h"

namespace bant {
// Tuple of all output streams to talk to the user.
class SessionStreams {
 public:
  SessionStreams(std::ostream *out, std::ostream *info)
      : out_(out), info_(info) {}  // Currently no dedicated error stream.

  std::ostream &out() { return *out_; }
  std::ostream &info() { return *info_; }
  std::ostream &error() { return *info_; }

 private:
  std::ostream *out_;
  std::ostream *info_;
};

// Command line flags needed by
struct CommandlineFlags {
  bool verbose = false;
  bool print_ast = false;
  bool print_only_errors = false;
  bool elaborate = false;
  int recurse_dependency_depth = 0;
  OutputFormat output_format = OutputFormat::kNative;
};

// A session contains some settings such as output/verbose requests
// as well as access to streams for general output or error and info messages.
// It is passed to functionality needing it without requring a global state.
class Session {
 public:
  using StatMap = OneToOne<std::string_view, std::unique_ptr<bant::Stat>>;

  Session(std::ostream *out, std::ostream *info, const CommandlineFlags &flags)
      : streams_(out, info), flags_(flags) {}

  SessionStreams &streams() { return streams_; }

  // Convenience accessors.
  std::ostream &out() { return streams_.out(); }
  std::ostream &info() { return streams_.info(); }
  std::ostream &error() { return streams_.error(); }

  // Deprecated. Use Flags directly.
  bool verbose() const { return flags_.verbose; }
  OutputFormat output_format() const { return flags_.output_format; }

  const CommandlineFlags &flags() const { return flags_; }

  // Get a stat object to fill/update. The "subsystem_name" describes who is
  // collecting stats, the "subject" is what (e.g. file-count etc).
  // Both strings needs to outlive this session object, so typically a regular
  // compile-time string constant.
  Stat &GetStatsFor(std::string_view subsystem_name, std::string_view subject) {
    auto inserted =
      stats_.insert({subsystem_name, std::make_unique<Stat>(subject)});
    if (inserted.second) {
      stat_init_key_order_.push_back(subsystem_name);
    }
    return *inserted.first->second;
  }

  // Return stat keys in the sequence they have been added.
  const std::vector<std::string_view> &stat_keys() const {
    return stat_init_key_order_;
  }

  // Get stat for subsystem or nullptr, if there is no such stat.
  const Stat *stat(std::string_view subsystem_name) const {
    auto found = stats_.find(subsystem_name);
    return (found != stats_.end()) ? found->second.get() : nullptr;
  }

 private:
  StatMap stats_;
  std::vector<std::string_view> stat_init_key_order_;
  SessionStreams streams_;
  CommandlineFlags flags_;
  OutputFormat output_format_;
};
}  // namespace bant
#endif  // BANT_SESSION_H
