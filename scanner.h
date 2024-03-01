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

#ifndef BANT_SCANNER_H_
#define BANT_SCANNER_H_

#include <ostream>
#include <string_view>

#include "linecolumn-map.h"

namespace bant {
enum TokenType : int {
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

  kAssign = '=',
  kLessThan = '<',
  kGreaterThan = '>',
  kNot = '!',

  // Relational operators with two characters. Needs to be above char range
  kEqualityComparison = '=' + 256,  // '=='
  kNotEqual = '!' + 256,            // '!='
  kLessEqual = '<' + 256,           // '<='
  kGreaterEqual = '>' + 256,        // '>='

  kIdentifier,

  kStringLiteral,
  kNumberLiteral,

  kFor,
  kIn,
  kNotIn,  // sequence of words 'not' and 'in'
  kIf,
  kElse,

  kError,  // Unexpected token.
  kEof,
};

std::ostream &operator<<(std::ostream &o, TokenType t);

struct Token {
  TokenType type;
  std::string_view text;  // Referring to original content.
};

std::ostream &operator<<(std::ostream &o, Token t);

class Scanner {
 public:
  // A scanner reading tokens from "content", updating "line_map".
  // It will update the line_map with all newlines it encounters; does not
  // take ownership of the LineColumnMap, so it can later be used any
  // time to determine the position of a Token extractedf from the file.
  // All tokens returned by the Scanner are substrings from the larger
  // content; this keeps correspondence with the original.
  Scanner(std::string_view content, LineColumnMap &line_map);

  // Return next token.
  Token Next();

  Token Peek() {
    if (!has_upcoming_) {
      upcoming_ = Next();
      has_upcoming_ = true;
    }
    return upcoming_;
  }

  const LineColumnMap &line_col() const { return line_map_; }

 private:
  using ContentPointer = std::string_view::const_iterator;

  inline ContentPointer SkipSpace();

  bool ConsumeOptionalIn();
  Token HandleNumber();
  Token HandleString();
  Token HandleIdentifierKeywordRawStringOrInvalid();
  Token HandleAssignOrRelational();
  Token HandleNotOrNotEquals();

  // Externally owned content.
  const std::string_view content_;
  ContentPointer pos_;

  // If we got a token from peeking, this is it.
  Token upcoming_;
  bool has_upcoming_ = false;

  LineColumnMap &line_map_;
};
}  // namespace bant
#endif  // BANT_SCANNER_H_
