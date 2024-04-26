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

#ifndef BANT_PARSER_H_
#define BANT_PARSER_H_

#include <memory>
#include <ostream>

#include "bant/frontend/ast.h"
#include "bant/frontend/scanner.h"
#include "bant/util/arena.h"

namespace bant {
class Parser {
 public:
  // Create a Parser for bazel-like files, consuming tokens from "token_source".
  // Memory for nodes is alloctaed from given "allocator" arena.
  // The "err_out" stream receives user-readable error messages.
  Parser(Scanner *token_source, Arena *allocator, std::ostream &err_out);

  ~Parser();

  // Consume token_source, parse file and return the abstract syntax tree root.
  // The toplevel returns a list of statements.
  // If there is an error, return at least partial tree to what
  // was possible to parse.
  //
  // All nodes are owned by the arena, all string_views are substrings of the
  // original source.
  // Callling parse() more than once is not supported.
  List *parse();

  // Returns if there was a parse error.
  bool parse_error() const;

 private:
  class Impl;
  const std::unique_ptr<Impl> impl_;
};
}  // namespace bant
#endif  // BANT_PARSER_H_
