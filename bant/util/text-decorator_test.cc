// bant - Bazel Navigation Tool
// Copyright (C) 2026 Henner Zeller <h.zeller@acm.org>
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

#include "bant/util/text-decorator.h"

#include <ostream>
#include <sstream>
#include <string_view>

#include "gtest/gtest.h"

namespace bant {
namespace {

TEST(TextDecorator, StartAlwaysComesAfterLastEnd) {
  // Even if something starts at the same offset another thing ends, the end
  // happens first.
  constexpr std::string_view kSampleText = "0123456789";
  TextDecorator decorator;
  // Before first, then after.
  decorator.AddDecoration(
    7, 1,  //
    [](std::ostream &o) { o << "<seven>"; },
    [](std::ostream &o) { o << "</seven>"; });
  decorator.AddDecoration(
    8, 1,  //
    [](std::ostream &o) { o << "<eight>"; },
    [](std::ostream &o) { o << "</eight>"; });

  // After fist, then before.
  decorator.AddDecoration(
    5, 1,  //
    [](std::ostream &o) { o << "<five>"; },
    [](std::ostream &o) { o << "</five>"; });
  decorator.AddDecoration(
    4, 1,  //
    [](std::ostream &o) { o << "<four>"; },
    [](std::ostream &o) { o << "</four>"; });

  std::stringstream out;
  decorator.Emit(kSampleText, out);
  EXPECT_EQ(out.str(),
            "0123<four>4</four><five>5</five>6"
            "<seven>7</seven><eight>8</eight>9");
}

// Due to the simplicity, nesting as nested begin/end. This documents it.
TEST(TextDecorator, SimpleNest) {
  constexpr std::string_view kSampleText = "0123456789";
  TextDecorator decorator;
  decorator.AddDecoration(
    5, 1,  //
    [](std::ostream &o) { o << "<buzz>"; },
    [](std::ostream &o) { o << "</buzz>"; });
  decorator.AddDecoration(
    5, 1,  //
    [](std::ostream &o) { o << "<prime>"; },
    [](std::ostream &o) { o << "</prime>"; });

  // Ideally, this would be nesting, and maybe even the thing that
  // was added first more closely. So <prime><buzz>5</buzz></prime>
  std::stringstream out;
  decorator.Emit(kSampleText, out);
  EXPECT_EQ(out.str(), "01234<buzz><prime>5</buzz></prime>6789");
}

TEST(TextDecorator, Overlap) {
  constexpr std::string_view kSampleText = "0123456789";
  TextDecorator decorator;
  decorator.AddDecoration(
    3, 5,  //
    [](std::ostream &o) { o << "<three-seven>"; },
    [](std::ostream &o) { o << "</three-seven>"; });

  decorator.AddDecoration(
    5, 4,  //
    [](std::ostream &o) { o << "<five-eight>"; },
    [](std::ostream &o) { o << "</five-eight>"; });

  // Overlap works as expected, but maybe we need to be more sophisticated in
  // the future as this is used for terminal text decoration: if a terminal
  // reset comes, it will reset all properties. So instead, if a new thing
  // starts, we might need to end the first thing, start the new thing and the
  // old thing again ... complicated and possibly rarely needed.
  std::stringstream out;
  decorator.Emit(kSampleText, out);
  EXPECT_EQ(out.str(),
            "012<three-seven>34<five-eight>567</three-seven>8</five-eight>9");
}

TEST(TextDecorator, AsymmetricEmitting) {
  constexpr std::string_view kSampleText = "0123456789";
  TextDecorator decorator;
  // Only one of the start/end callables are active.
  decorator.AddDecoration(4, 1, [](std::ostream &o) { o << "(four →)"; }, {});
  decorator.AddDecoration(7, 1, {}, [](std::ostream &o) { o << "(← seven)"; });

  std::stringstream out;
  decorator.Emit(kSampleText, out);
  EXPECT_EQ(out.str(), "0123(four →)4567(← seven)89");
}

}  // namespace
}  // namespace bant
