// bant - Bazel Navigation Tool
// Copyright (C) 2024 Henner Zeller <h.zeller@acm.org>

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "scanner.h"

#include <gtest/gtest.h>

namespace bant {
inline bool operator==(const Token &a, const Token &b) {
  return a.type == b.type && a.text == b.text;
}

TEST(ScannerTest, EmptyStringEOF) {
  Scanner s("");
  EXPECT_EQ(s.Next().type, TokenType::kEof);
  EXPECT_EQ(s.Next().type, TokenType::kEof);
}

TEST(ScannerTest, UnknownToken) {
  Scanner s("@");
  EXPECT_EQ(s.Next().type, TokenType::kError);
  EXPECT_EQ(s.Next().type, TokenType::kEof);
}

TEST(ScannerTest, NumberString) {
  Scanner s(R"(42 "hello world")");
  EXPECT_EQ(s.Next(), Token({TokenType::kNumberLiteral, "42"}));
  EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, "\"hello world\""}));
  EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
}

TEST(ScannerTest, StringLiteral) {
  {
    Scanner s(R"("double")");
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, R"("double")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
  {
    Scanner s(R"('single')");
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, R"('single')"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
  {
    Scanner s(R"("hello \" ' world")");
    EXPECT_EQ(s.Next(),
              Token({TokenType::kStringLiteral, R"("hello \" ' world")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }

  {
    Scanner s(R"('hello " \' world')");
    EXPECT_EQ(s.Next(),
              Token({TokenType::kStringLiteral, R"('hello " \' world')"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }

  {  // long string literals
    Scanner s(R"("""hello "" world""")");
    EXPECT_EQ(s.Next(),
              Token({TokenType::kStringLiteral, R"("""hello "" world""")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
  {
    Scanner s(R"("""""")");
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, R"("""""")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
  {
    Scanner s(R"(""""")");
    EXPECT_EQ(s.Next(), Token({TokenType::kError, R"(""""")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
}
}  // namespace bant