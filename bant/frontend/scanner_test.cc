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

#include "bant/frontend/scanner.h"

#include <string_view>

#include "bant/frontend/named-content.h"
#include "gtest/gtest.h"

namespace bant {
inline bool operator==(const Token &a, const Token &b) {
  return a.type == b.type && a.text == b.text;
}

#define LSTR(x)  LSTR1(x)
#define LSTR1(x) #x
#define TEST_SCANNER(name, content)                                       \
  NamedLineIndexedContent content_##__LINE__(__FILE__ ":" LSTR(__LINE__), \
                                             content);                    \
  Scanner name(content_##__LINE__)

TEST(ScannerTest, EmptyStringEOF) {
  TEST_SCANNER(s, "");
  EXPECT_EQ(s.Next().type, TokenType::kEof);
  EXPECT_EQ(s.Next().type, TokenType::kEof);
}

TEST(ScannerTest, JustCommentThenEOF) {
  TEST_SCANNER(s, " # foo");
  EXPECT_EQ(s.Next().type, TokenType::kEof);
  EXPECT_EQ(s.Next().type, TokenType::kEof);
}

TEST(ScannerTest, UnknownToken) {
  TEST_SCANNER(s, "@");
  EXPECT_EQ(s.Next().type, TokenType::kError);
  EXPECT_EQ(s.Next().type, TokenType::kEof);
}

TEST(ScannerTest, BackslashSimplySkippedAsWhitespace) {
  TEST_SCANNER(s, R"(if\else)");
  EXPECT_EQ(s.Next().type, TokenType::kIf);
  EXPECT_EQ(s.Next().type, TokenType::kElse);
  EXPECT_EQ(s.Next().type, TokenType::kEof);
}

TEST(ScannerTest, SimpleTokens) {
  struct TestCase {
    std::string_view input_text;
    TokenType expected;
  };
  constexpr TestCase tests[] = {
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
    {"not in", TokenType::kNotIn},
    {"not  in", TokenType::kNotIn},
    {"if", TokenType::kIf},
    {"else", TokenType::kElse},
    {"some_random_thing", TokenType::kIdentifier},
  };
  for (const TestCase &t : tests) {
    TEST_SCANNER(s, t.input_text);
    const Token tok = s.Next();
    EXPECT_EQ(tok.type, t.expected);
    EXPECT_EQ(tok.text, t.input_text);
    EXPECT_EQ(s.Next().type, TokenType::kEof);
  }
}

TEST(ScannerTest, NumberString) {
  TEST_SCANNER(s, R"(42 "hello world")");
  EXPECT_EQ(s.Next(), Token({TokenType::kNumberLiteral, "42"}));
  EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, "\"hello world\""}));
  EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
}

TEST(ScannerTest, DoubleWordTokens) {
  TEST_SCANNER(s, R"(43 not in answer foo in 12)");
  EXPECT_EQ(s.Next(), Token({TokenType::kNumberLiteral, "43"}));
  EXPECT_EQ(s.Next(), Token({TokenType::kNotIn, "not in"}));
  EXPECT_EQ(s.Next(), Token({TokenType::kIdentifier, "answer"}));
  EXPECT_EQ(s.Next(), Token({TokenType::kIdentifier, "foo"}));
  EXPECT_EQ(s.Next(), Token({TokenType::kIn, "in"}));
  EXPECT_EQ(s.Next(), Token({TokenType::kNumberLiteral, "12"}));
  EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
}

TEST(ScannerTest, StringLiteral) {
  {
    TEST_SCANNER(s, R"("double")");
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, R"("double")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
  {
    TEST_SCANNER(s, R"('single')");
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, R"('single')"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
  {
    TEST_SCANNER(s, R"("hello \" ' world")");
    EXPECT_EQ(s.Next(),
              Token({TokenType::kStringLiteral, R"("hello \" ' world")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }

  {
    TEST_SCANNER(s, R"('hello " \' world')");
    EXPECT_EQ(s.Next(),
              Token({TokenType::kStringLiteral, R"('hello " \' world')"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }

  {
    TEST_SCANNER(s, R"("\\")");
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, R"("\\")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }

  {
    TEST_SCANNER(s, R"("\\\\")");
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, R"("\\\\")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }

  {  // long string literals
    TEST_SCANNER(s, R"("""hello "" world""")");
    EXPECT_EQ(s.Next(),
              Token({TokenType::kStringLiteral, R"("""hello "" world""")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
  {
    TEST_SCANNER(s, R"("""""")");
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, R"("""""")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
  {
    TEST_SCANNER(s, R"(""""")");
    EXPECT_EQ(s.Next(), Token({TokenType::kError, R"(""""")"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
}

TEST(ScannerTest, RawStringLiteral) {
  {
    TEST_SCANNER(s, "  r'foo'  ");
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, "r'foo'"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
  {
    TEST_SCANNER(s, "r''");
    EXPECT_EQ(s.Next(), Token({TokenType::kStringLiteral, "r''"}));
    EXPECT_EQ(s.Next(), Token({TokenType::kEof, ""}));
  }
}

}  // namespace bant
