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

#include "bant/frontend/macro-substitutor.h"

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "bant/frontend/parsed-project_testutil.h"
#include "bant/session.h"
#include "gtest/gtest.h"

namespace bant {
// TODO: the Elaborator test also has something similar. Unify ?
class MacroSubstituteTest : public testing::Test {
 public:
  std::string MacroSubstituteAndPrint(std::string_view to_substitute) {
    const CommandlineFlags flags = CommandlineFlags{.verbose = 1};
    substituted_ = pp_.Add("//substitute", to_substitute);

    Session session(&std::cerr, &std::cerr, flags);
    std::stringstream subst_print;
    subst_print << MacroSubstitute(session, &pp_.project(), substituted_->ast);
    // TODO: add toplevel tuple parsing in parser, so that we can actually
    // provide an expected version.
    return subst_print.str();
  }

 private:
  ParsedProjectTestUtil pp_;
  const ParsedBuildFile *substituted_ = nullptr;
};

TEST_F(MacroSubstituteTest, BasicTest) {
  const std::string result = MacroSubstituteAndPrint(R"(
test_example(name = "foobar")
)");
  EXPECT_EQ(result, R"([(genrule(
            name = "foobar" + "-gen",
            outs = ["foobar" + "-gen.cc"]
        ),)])");
}
}  // namespace bant
