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

#include <cassert>
#include <string_view>

#include "linecolumn-map.h"
#include "absl/strings/ascii.h"

namespace bant {
std::ostream &operator<<(std::ostream &o, TokenType t) {
  switch (t) {
  case '(':
  case ')':
  case '{':
  case '}':
  case '[':
  case ']':
  case ':':
  case ',':
  case '=':
  case '+':
  case '-':
  case '.':
  case '%': o << (char)t; break;

  case TokenType::kIdentifier: o << "ident"; break;
  case TokenType::kStringLiteral: o << "string"; break;
  case TokenType::kRawStringLiteral: o << "rawstring"; break;
  case TokenType::kNumberLiteral: o << "number"; break;
  case TokenType::kFor: o << "for"; break;
  case TokenType::kIn: o << "in"; break;
  case TokenType::kIf: o << "if"; break;
  case TokenType::kElse: o << "else"; break;
  case TokenType::kError: o << "<<ERROR>>"; break;
  case TokenType::kEof: o << "<<EOF>>"; break;
  }
  return o;
}

std::ostream &operator<<(std::ostream &o, const Token t) {
  o << t.type << "('" << t.text << "')";
  return o;
}

Scanner::Scanner(std::string_view content, LineColumnMap *line_map)
    : content_(content), pos_(content_.begin()), line_map_(line_map) {
  assert(line_map_ != nullptr);
  assert(line_map_->empty());  // Already used ?
  line_map_->PushNewline(pos_);
}

inline Scanner::ContentPointer Scanner::SkipSpace() {
  for (;;) {
    while (pos_ < content_.end() && absl::ascii_isspace(*pos_)) {
      ++pos_;
    }
    if (*pos_ != '#') break;
    // In comment now. Skip.
    while (pos_ < content_.end() && *pos_ != '\n') {
      ++pos_;
    }
  }
  return pos_;
}

static bool IsIdentifierChar(char c) {
  return absl::ascii_isdigit(c) || absl::ascii_isalpha(c) || c == '_';
}

Token Scanner::HandleIdentifierKeywordRawStringOrInvalid() {
  const ContentPointer start = pos_;

  // Raw string literals r"foo" start out looking like an identifier,
  // but the following quote gives it away.
  if ((content_.end() - start) > 2 &&          //
      (start[0] == 'r' || start[0] == 'R') &&  //
      (start[1] == '"' || start[1] == '\'')) {
    ++pos_;
    return HandleString(TokenType::kRawStringLiteral);
  }

  // Digit already ruled out at this point as first character.
  if (!IsIdentifierChar(*start)) {
    ++pos_;
    return {TokenType::kError, {start, 1}};
  }
  while (pos_ < content_.end() && IsIdentifierChar(*pos_)) {
    ++pos_;
  }
  const std::string_view text{start, (size_t)(pos_ - start)};

  // Keywords, anything else will be an identifier.
  if (text == "in") return {TokenType::kIn, text};
  if (text == "for") return {TokenType::kFor, text};
  if (text == "if") return {TokenType::kIf, text};
  if (text == "else") return {TokenType::kElse, text};

  return {TokenType::kIdentifier, text};
}

Token Scanner::HandleString(TokenType str_token) {
  bool triple_quote = false;
  const ContentPointer start = pos_;
  const char str_quote = *pos_;
  pos_++;
  if (pos_ + 1 < content_.end() && *pos_ == str_quote &&
      *(pos_ + 1) == str_quote) {
    triple_quote = true;
    pos_ += 2;
  }
  int close_quote_count = triple_quote ? 3 : 1;
  bool last_was_escape = false;
  while (pos_ < content_.end()) {
    if (*pos_ == str_quote && !last_was_escape) {
      --close_quote_count;
      if (close_quote_count == 0) break;
    } else {
      close_quote_count = triple_quote ? 3 : 1;
    }
    last_was_escape = (*pos_ == '\\');
    if (*pos_ == '\n') {
      line_map_->PushNewline(pos_ + 1);
    }
    ++pos_;
  }
  if (pos_ >= content_.end()) {
    return {TokenType::kError, {start, (size_t)(pos_ - start)}};
  }
  ++pos_;
  return {str_token, {start, (size_t)(pos_ - start)}};
}

Token Scanner::HandleNumber() {
  const ContentPointer start = pos_;
  bool dot_seen = false;
  ++pos_;
  while (pos_ < content_.end() && (isdigit(*pos_) || *pos_ == '.')) {
    if (*pos_ == '.') {
      if (dot_seen) {
        return {TokenType::kError, {start, (size_t)(pos_ - start)}};
      }
      dot_seen = true;
    }
    ++pos_;
  }
  return {TokenType::kNumberLiteral, {start, (size_t)(pos_ - start)}};
}

Token Scanner::Next() {
  if (has_upcoming_) {
    // We were already called in Peek(). Flush that token.
    has_upcoming_ = false;
    return upcoming_;
  }

  if (SkipSpace() == content_.end()) {
    return {TokenType::kEof, {content_.end(), 0}};
  }
  Token result;
  switch (*pos_) {
  case '(':
  case ')':
  case '{':
  case '}':
  case '[':
  case ']':
  case ',':
  case ':':
  case '=':
  case '+':
  case '-':
  case '.':
  case '%':
    result = {/*.type =*/(TokenType)*pos_, /*.text =*/{pos_, 1}};
    ++pos_;
    break;

  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9': result = HandleNumber(); break;

  case '"':
  case '\'': result = HandleString(TokenType::kStringLiteral); break;

  default: result = HandleIdentifierKeywordRawStringOrInvalid(); break;
  }
  return result;
}
}  // namespace bant
