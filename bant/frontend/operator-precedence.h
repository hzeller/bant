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

#ifndef BANT_FRONTEND_OPERATOR_PRECEDENCE_H_
#define BANT_FRONTEND_OPERATOR_PRECEDENCE_H_

#include <initializer_list>

#include "bant/frontend/scanner.h"

namespace bant {

struct OperatorLevel {
  bool is_unary;
  std::initializer_list<TokenType> tokens;
};

// Operator precedence from strongest (0) to weakest.
inline constexpr OperatorLevel kPrecedenceList[] = {
  {false, {}},      // 0: handled by ParseAtom()
  {false, {kDot}},  // 1: scoped invocation (and array subscript '[' internally)
  {true, {kMinus}},                                       // 2: unary -
  {false, {kMultiply, kDivide, kFloorDivide, kPercent}},  // 3
  {false, {kPlus, kMinus}},                               // 4
  {false, {kShiftLeft, kShiftRight}},                     // 5
  {false, {kPipeOrBitwiseOr}},                            // 6
  {false,
   {kLessThan, kLessEqual, kEqualityComparison, kGreaterEqual, kGreaterThan,
    kNotEqual, kIn, kNotIn}},  // 7
  {true, {kNot}},              // 8: unary not
  {false, {kAnd}},             // 9
  {false, {kOr}},              // 10
};

inline constexpr int kLowestBinaryPrecedence =
  sizeof(kPrecedenceList) / sizeof(kPrecedenceList[0]) - 1;

// Ternary if-else has lower precedence than 'or'.
inline constexpr int kTernaryPrecedence = kLowestBinaryPrecedence + 1;

// The weakest possible precedence level, used for statement contexts
// or leaf nodes where parenthesis are never required.
inline constexpr int kLowestPrecedence = kTernaryPrecedence + 1;

}  // namespace bant

#endif  // BANT_FRONTEND_OPERATOR_PRECEDENCE_H_
