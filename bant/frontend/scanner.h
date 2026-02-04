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

#ifndef BANT_SCANNER_H_
#define BANT_SCANNER_H_

#include <cstdint>
#include <ostream>
#include <string_view>

#include "bant/frontend/named-content.h"

namespace bant {
enum TokenType : int {  // NOLINT(readability-enum-initial-value)
  // As-is tokens
  kOpenParen = '(',
  kCloseParen = ')',
  kOpenSquare = '[',
  kCloseSquare = ']',
  kOpenBrace = '{',
  kCloseBrace = '}',
  kComma = ',',
  kColon = ':',
  kPlus = '+',
  kMinus = '-',
  kMultiply = '*',
  kDivide = '/',
  kDot = '.',
  kPercent = '%',
  kPipeOrBitwiseOr = '|',

  kAssign = '=',
  kLessThan = '<',
  kGreaterThan = '>',
  kNot = '!',

  // Operators with two characters. Needs to be above char range
  kFloorDivide = '/' + 256,         // '//' a.k.a integer division
  kEqualityComparison = '=' + 256,  // '=='
  kNotEqual = '!' + 256,            // '!='
  kLessEqual = '<' + 256,           // '<='
  kGreaterEqual = '>' + 256,        // '>='

  kIdentifier = 512,

  kShiftLeft = '<' + 512,   // <<  more things that start with >
  kShiftRight = '>' + 512,  // >>

  kStringLiteral,
  kNumberLiteral,
  kDefBlock,  // uninterpreted

  kFor,
  kIn,
  kNotIn,  // sequence of words 'not' and 'in'
  kAnd,
  kOr,
  kIf,
  kElse,

  kError,  // Unexpected token.
  kEof,
};

std::ostream &operator<<(std::ostream &o, TokenType t);

struct Token {
  constexpr Token() = default;
  constexpr Token(TokenType type, std::string_view text,
                  bool newline_since_last = false)
      : text(text), type(type), newline_since_last_token(newline_since_last) {}
  // NB: order of fields is for better packing of struct.
  std::string_view text;                  // Referring to original content.
  TokenType type;                         // token id
  bool newline_since_last_token = false;  // to accomodate Python-ism's
};

std::ostream &operator<<(std::ostream &o, Token t);

class Scanner {
 public:
  // A scanner reading tokens from the content of source and updating
  // the source-line index with newlines it encounters.
  // All tokens returned by the Scanner are sub-string_views of the larger
  // content; this allows correspondence with the original text to extract
  // source.Loc() information.
  explicit Scanner(NamedLineIndexedContent &source);

  // Advance to next token and return it.
  Token Next();

  // Peek next token and return, but don't advance yet.
  Token Peek() {
    if (!has_upcoming_) {
      upcoming_ = Next();
      has_upcoming_ = true;
    }
    return upcoming_;
  }

  const NamedLineIndexedContent &source() { return source_; }

 private:
  using ContentPointer = std::string_view::const_iterator;

  inline ContentPointer SkipSpace();

  bool ConsumeOptionalIn();
  Token ConsumeEverythingIndentedAsDefBlock(ContentPointer start);
  Token HandleNumber();
  Token HandleString();
  Token HandleIdentifierKeywordRawStringOrInvalid();
  Token HandleAssignOrRelationalOrShift();
  Token HandleNotOrNotEquals();
  Token HandleDivideOrFloorDivide();

  NamedLineIndexedContent &source_;
  const ContentPointer end_;  // End of input.

  ContentPointer pos_;  // Current scanning location

  // If we got a token from peeking, this is it.
  Token upcoming_;
  bool has_upcoming_ = false;
  uint32_t newline_count_ = 0;
  uint32_t last_token_newline_count_ = 0;
};
}  // namespace bant
#endif  // BANT_SCANNER_H_
