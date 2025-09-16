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

#ifndef BANT_SESSION_H
#define BANT_SESSION_H

#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
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

// Command line flags filled in main(), used by tools (some only needed
// in some commands)
struct CommandlineFlags {
  int verbose = 0;
  bool print_ast = false;  // Print ast instead of just rules.
  bool print_only_errors = false;
  bool elaborate = false;
  bool builtin_macro_expand = false;
  bool ignore_keep_comment = false;
  bool only_physical_files = false;  // for target-{srcs,hdrs,data}
  int recurse_dependency_depth = 0;
  OutputFormat output_format = OutputFormat::kNative;
  int io_threads = 0;  // <= 0: synchronous operation.
  std::string grep_regex;
  bool do_color = false;
  // https://bazel.build/docs/configurable-attributes#custom-flags
  absl::flat_hash_set<std::string> custom_flags;
  std::string direct_filename;  // internal debugging feature: just parse file
};

// A session contains some settings such as output/verbose requests
// as well as access to streams for general output or error and info messages.
// It is passed to functionality needing it without requring a global state.
class Session {
 public:
  using StatMap = OneToOne<std::string_view, std::unique_ptr<bant::Stat>>;

  Session(std::ostream *out, std::ostream *info, CommandlineFlags flags)
      : streams_(out, info), flags_(std::move(flags)) {}

  SessionStreams &streams() { return streams_; }

  // Convenience accessors.
  std::ostream &out() { return streams_.out(); }
  std::ostream &info() { return streams_.info(); }
  std::ostream &error() { return streams_.error(); }

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
};
}  // namespace bant
#endif  // BANT_SESSION_H
