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

#ifndef BANT_PARSED_PROJECT_TESTUTIL_
#define BANT_PARSED_PROJECT_TESTUTIL_

#include <iostream>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "bant/frontend/elaboration.h"
#include "bant/frontend/parsed-project.h"
#include "bant/session.h"
#include "bant/types-bazel.h"

namespace bant {
class ParsedProjectTestUtil {
 public:
  // Add a file with the given bazel package path and content to the
  // ParsedProject. Returns the parsed build file.
  const ParsedBuildFile *Add(std::string_view package_str,
                             std::string_view content) {
    auto package_or = BazelPackage::ParseFrom(package_str);
    if (!package_or.has_value()) return nullptr;
    SessionStreams streams(&std::cerr, &std::cerr);
    const std::string fake_filename = absl::StrCat(package_str, "/BUILD");
    return project_.AddBuildFileContent(streams, *package_or, fake_filename,
                                        std::string(content));
  }

  // The project.
  ParsedProject &project() { return project_; }

  void SetMacroContent(std::string_view macros) {
    CHECK_OK(project_.SetBuiltinMacroContent(macros));
  }

  void ElaborateAll() {
    Session session(&std::cerr, &std::cerr, CommandlineFlags{.verbose = 1});
    const ElaborationOptions elab_options{.builtin_macro_expansion = true};
    Elaborate(session, &project_, elab_options);
  }

 private:
  ParsedProject project_{{}, false, false};
};
}  // namespace bant

#endif  // BANT_PARSED_PROJECT_TESTUTIL_
