// bant - Bazel Navigation Tool
// Copyright (C) 2025 Henner Zeller <h.zeller@acm.org>
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

#include "bant/util/grep-highlighter.h"

#include <sstream>

#include "gtest/gtest.h"

namespace bant {
namespace {
TEST(GrepHighlighterTest, SimpleMatch) {
  GrepHighlighter highligher(false, true);
  std::stringstream sink;
  EXPECT_TRUE(highligher.AddExpressions({"ello"}, false, sink));

  EXPECT_TRUE(GrepHighlight(highligher, "hello world", sink,
                            HighlightWhat::kHighlightAndFilter, "start>",
                            "<end"));
  EXPECT_FALSE(GrepHighlight(highligher, "nothing here", sink));
  EXPECT_EQ(sink.str(), "start>hello world<end");
}

TEST(GrepHighlighterTest, SimpleExludeMatch) {
  // 'grep -v'
  GrepHighlighter highligher(false, true);
  std::stringstream sink;
  EXPECT_TRUE(highligher.AddExcludeExpressions({"ello"}, false, sink));

  EXPECT_FALSE(GrepHighlight(highligher, "hello world", sink));
  EXPECT_TRUE(GrepHighlight(highligher, "nothing here", sink));

  EXPECT_EQ(sink.str(), "nothing here");
}

TEST(GrepHighlighterTest, HighlightMatch) {
  GrepHighlighter highligher(true, true);
  std::stringstream sink;
  EXPECT_TRUE(highligher.AddExpressions({"ello", "rld"}, false, sink));
  highligher.SetHighlightStart({"_RED_", "_GREEN_", "_BLUE_"});
  highligher.SetHighlightEnd("_END_");

  EXPECT_TRUE(GrepHighlight(highligher, "hello world", sink));
  EXPECT_EQ(sink.str(), "h_RED_ello_END_ wo_GREEN_rld_END_");
}

TEST(GrepHighlighterTest, HighlightButtingUpMatch) {
  GrepHighlighter highligher(true, true);
  std::stringstream sink;
  EXPECT_TRUE(highligher.AddExpressions({"hello", "world"}, false, sink));
  highligher.SetHighlightStart({"_RED_", "_GREEN_", "_BLUE_"});
  highligher.SetHighlightEnd("_END_");

  EXPECT_TRUE(GrepHighlight(highligher, "helloworld", sink));
  EXPECT_EQ(sink.str(), "_RED_hello_END__GREEN_world_END_");
}

TEST(GrepHighlighterTest, AlwaysResetFirstButtingUpMatch) {
  GrepHighlighter highligher(true, true);
  std::stringstream sink;
  EXPECT_TRUE(highligher.AddExpressions({"world", "hello"}, false, sink));
  highligher.SetHighlightStart({"_RED_", "_GREEN_", "_BLUE_"});
  highligher.SetHighlightEnd("_END_");

  EXPECT_TRUE(GrepHighlight(highligher, "helloworld", sink));
  EXPECT_EQ(sink.str(), "_GREEN_hello_END__RED_world_END_");
}

TEST(GrepHighlighterTest, InvalidExpression) {
  GrepHighlighter highligher(true, true);
  std::stringstream sink;
  EXPECT_FALSE(highligher.AddExpressions({"hello("}, false, sink));
  EXPECT_EQ(sink.str(), "Grep pattern: missing ): (hello()\n");
}

}  // namespace
}  // namespace bant
