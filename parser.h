#pragma once

#include <iostream>
#include <memory>

#include "arena.h"
#include "ast.h"
#include "scanner.h"

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
