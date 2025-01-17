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

#ifndef BANT_CLI_COMMANDS
#define BANT_CLI_COMMANDS

#include <span>
#include <string_view>

#include "bant/session.h"

namespace bant {
enum class CliStatus {
  kExitSuccess = 0,
  kExitFailure = 1,
  kExitCommandlineClarification = 2,  // command line underspecified.
  kExitCleanupFindings = 3,           // There were findings in cleanup
};
CliStatus RunCliCommand(Session &session, std::span<std::string_view> args);
}  // namespace bant
#endif  // BANT_CLI_COMMANDS
