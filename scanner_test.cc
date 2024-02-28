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

#include "scanner.h"

#include "gtest/gtest.h"

namespace bant {
inline bool operator==(const Token &a, const Token &b) {
  return a.type == b.type && a.text == b.text;
}

TEST(ScannerTest, EmptyStringEOF) {
  LineColumnMap lc;
  Scanner s("", lc);
  EXPECT_EQ(s.Next().type, TokenType::kEof);
  EXPECT_EQ(s.Next().type, TokenType::kEof);
}

TEST(ScannerTest, UnknownToken) {
  LineColumnMap lc;
  Scanner s("@", lc);
  EXPECT_EQ(s.Next().type, TokenType::kError);
  EXPECT_EQ(s.Next().type, TokenType::kEof);
}

TEST(ScannerTest, SimpleTokens) {
  struct TestCase {
    std::string_view input_text;
    TokenType expected;
  };
  TestCase tests[] = {
    {"(", TokenType::kOpenParen},
    {")", TokenType::kCloseParen},
    {"[", TokenType::kOpenSquare},
    {"]", TokenType::kCloseSquare},
    {"{", TokenType::kOpenBrace},
    {"}", TokenType::kCloseBrace},
    {",", TokenType::kComma},
    {":", TokenType::kColon},
    {"+", TokenType::kPlus},
    {"-", TokenType::kMinus},
    {"*", TokenType::kMultiply},
    {"/", TokenType::kDivide},
    {".", TokenType::kDot},
    {"%", TokenType::kPercent},
    {"=", TokenType::kAssign},
    {"==", TokenType::kEqualityComparison},
    {"!=", TokenType::kNotEqual},
    {"<=", TokenType::kLessEqual},
    {">=", TokenType::kGreaterEqual},
    {">", TokenType::kGreaterThan},
    {"<", TokenType::kLessThan},
    // Identifiers or keywords
    {"not", TokenType::kNot},
    {"!", TokenType::kNot},
    {"for", TokenType::kFor},
    {"in", TokenType::kIn},
    {"if", TokenType::kIf},
    {"else", TokenType::kElse},
    {"some_random_thing", TokenType::kIdentifier},
  };
  for (const TestCase &t : tests) {
    LineColumnMap lc;
    Scanner s(t.input_text, lc);
    const Token tok = s.Next();
    EXPECT_EQ(tok.type, t.expected);
    EXPECT_EQ(tok.text, t.input_text);
    EXPECT_EQ(s.Next().type, TokenType::kEof);
  }
}

TEST(ScannerTest, NumberString) {
  LineColumnMap lc;
  Scanner s(R"(42 "hello world")", lc);
  EXPECT_EQ(s.Next(), Token({TokenType::kNumberLiteral, "42"}));
  EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, "\"hello world\""}));
  EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
}

TEST(ScannerTest, StringLiteral) {
  {
    LineColumnMap lc;
    Scanner s(R"("double")", lc);
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, R"("double")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
  {
    LineColumnMap lc;
    Scanner s(R"('single')", lc);
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, R"('single')"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
  {
    LineColumnMap lc;
    Scanner s(R"("hello \" ' world")", lc);
    EXPECT_EQ(s.Next(),
              Token({TokenType::kStringLiteral, R"("hello \" ' world")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }

  {
    LineColumnMap lc;
    Scanner s(R"('hello " \' world')", lc);
    EXPECT_EQ(s.Next(),
              Token({TokenType::kStringLiteral, R"('hello " \' world')"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }

  {  // long string literals
    LineColumnMap lc;
    Scanner s(R"("""hello "" world""")", lc);
    EXPECT_EQ(s.Next(),
              Token({TokenType::kStringLiteral, R"("""hello "" world""")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
  {
    LineColumnMap lc;
    Scanner s(R"("""""")", lc);
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, R"("""""")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
  {
    LineColumnMap lc;
    Scanner s(R"(""""")", lc);
    EXPECT_EQ(s.Next(), Token({TokenType::kError, R"(""""")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
}

TEST(ScannerTest, RawStringLiteral) {
  {
    LineColumnMap lc;
    Scanner s("  r'foo'  ", lc);
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, "r'foo'"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
  {
    LineColumnMap lc;
    Scanner s("r''", lc);
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, "r''"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
}

}  // namespace bant
