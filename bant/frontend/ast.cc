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

#include "bant/frontend/ast.h"

#include <charconv>
#include <cstdint>
#include <string_view>
#include <system_error>

#include "bant/util/arena.h"

namespace bant {
IntScalar *IntScalar::FromLiteral(Arena *arena, std::string_view literal) {
  int64_t val = 0;
  const std::string_view string_rep = literal;
  int base = 10;
  if (literal.size() >= 2) {
    switch (literal[1]) {
    case 'o':
      base = 8;
      literal.remove_prefix(2);
      break;
    case 'x':
      base = 16;
      literal.remove_prefix(2);
      break;
    default:;
    }
  }
  auto result = std::from_chars(literal.begin(), literal.end(), val, base);
  if (result.ec != std::errc{}) {
    return nullptr;
  }
  return arena->New<IntScalar>(string_rep, val);
}

StringScalar *StringScalar::FromLiteral(Arena *arena,
                                        std::string_view literal) {
  bool is_raw = false;
  bool is_triple_quoted = false;
  if (literal[0] == 'r' || literal[0] == 'R') {
    is_raw = true;
    literal.remove_prefix(1);
  }
  if (literal.length() >= 6 && literal.substr(0, 3) == R"(""")") {
    is_triple_quoted = true;
    literal = literal.substr(3);
    literal.remove_suffix(3);
  } else {
    literal = literal.substr(1);
    literal.remove_suffix(1);
  }

  // The string itself might still contain escaping characters, so anyone
  // using it might need to unescape it.
  // Within the Scalar, we keep the original string_view so that it is possible
  // to report location using the LineColumnMap.
  return arena->New<StringScalar>(literal, is_triple_quoted, is_raw);
}

}  // namespace bant
