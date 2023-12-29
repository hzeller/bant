#include "scanner.h"

#include <gtest/gtest.h>

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
