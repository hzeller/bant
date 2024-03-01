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
  // Create a Parser for bazel-like files, reading tokens from "token_source".
  // Memory for nodes is alloctaed from given "allocator"
  // arena. The "info_filename" is used to report errors, the "err_out"
  // stream receives user-readable error messages.
  Parser(Scanner *token_source, Arena *allocator, const char *info_filename,
         std::ostream &err_out);
  ~Parser();

  // Parse file and return an ast. The toplevel returns a list of
  // statements.
  // If there is an error, return at least partial tree to what
  // was possible to parse. In thee case of an error, the lastToken() will
  // return the token seen last.
  List *parse();

  // Returns if there was a parse error.
  bool parse_error() const;

  // Error token or kEof
  Token lastToken() const;

 private:
  class Impl;
  const std::unique_ptr<Impl> impl_;
};
}  // namespace bant
#endif  // BANT_PARSER_H_
