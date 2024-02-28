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

// next steps
//  - triple-string.
//  - list comprehension can have multiple 'for' in sequence

#include "parser.h"

#include <functional>
#include <iostream>

#include "ast.h"
#include "scanner.h"

// Set to 1 to get a parse tree trace.
#if 0
static int sNodeNum = 0;
static int sIndent = 0;
class NestingLogger {
public:
  NestingLogger(const char *fun, const Token &tok) {
    ++sNodeNum;
    ++sIndent;
    std::cerr << sNodeNum << std::string(2 * sIndent, '.') << fun
              << " " << tok << "\n";
  }
  ~NestingLogger() {
    --sIndent;
  }
};

#define LOG_ENTER() NestingLogger _log(__FUNCTION__, scanner_->Peek())
#else
#define LOG_ENTER()
#endif

namespace bant {
// Simple recursive descent parser. As Parser::Impl to not clobber the header
// file with all the parse methods needed for each production.
class Parser::Impl {
 public:
  Impl(Scanner *token_source, Arena *allocator, const char *info_filename,
       std::ostream &err_out)
      : scanner_(token_source),
        node_arena_(allocator),
        filename_(info_filename),
        err_out_(err_out) {}

  // Parse file. If there is an error, return at least partial tree.
  // A file is a list of data structures or identifiers.
  List *parse() {
    LOG_ENTER();
    if (previous_parse_result_) return previous_parse_result_;

    List *statement_list = Make<List>(List::Type::kList);
    previous_parse_result_ = statement_list;
    while (!error_) {
      auto tok = scanner_->Next();
      if (tok.type == kEof) {
        last_token_ = tok;
        return statement_list;
      }
      if (tok.type == kStringLiteral) {
        continue;  // Pythonism: Toplevel document no-effect statement
      }

      if (tok.type == '[') {
        statement_list->Append(node_arena_, ParseArrayOrListComprehension());
        continue;
      }

      // No other toplevel parts expected for now
      if (tok.type != kIdentifier) {
        ErrAt(tok) << "expected identifier\n";
        return statement_list;
      }

      // Got identifier, next step: either function call or assignment.
      auto after_id = scanner_->Next();
      switch (after_id.type) {
      case TokenType::kAssign:
        statement_list->Append(node_arena_,
                               ParseAssignmentRhs(Make<Identifier>(tok.text)));
        break;
      case TokenType::kOpenParen:
        statement_list->Append(node_arena_, ParseFunCall(tok));
        break;
      case TokenType::kDot:
        statement_list->Append(
          node_arena_, Make<BinOpNode>(Make<Identifier>(tok.text),
                                       ParseExpression(), TokenType::kDot));
        break;
      default:
        ErrAt(after_id) << "expected `(` or `=`\n";
        return statement_list;
      }
    }
    return statement_list;
  }

  Assignment *ParseAssignmentRhs(Identifier *id) {
    LOG_ENTER();
    // '=' already consumed
    return Make<Assignment>(id, ParseExpression());
  }

  FunCall *ParseFunCall(Token identifier) {
    LOG_ENTER();
    // opening '(' already consumed.
    List *args = ParseList(
      Make<List>(List::Type::kTuple),
      [&]() { return ExpressionOrAssignment(); }, TokenType::kCloseParen);
    return Make<FunCall>(Make<Identifier>(identifier.text), args);
  }

  List *ParseList(List *result, const std::function<Node *()> &element_parse,
                  TokenType end_tok) {
    LOG_ENTER();
    Token upcoming = scanner_->Peek();
    while (upcoming.type != end_tok) {
      result->Append(node_arena_, element_parse());
      upcoming = scanner_->Peek();
      if (upcoming.type == ',') {
        scanner_->Next();
        upcoming = scanner_->Peek();
      } else if (upcoming.type != end_tok) {
        ErrAt(scanner_->Next())
          << "expected `,` or closing `" << end_tok << "`\n";
        return result;
      }
    }
    scanner_->Next();  // eats end_tok
    return result;
  }

  Node *ExpressionOrAssignment() {
    LOG_ENTER();
    Node *value = ParseExpression();
    Token upcoming = scanner_->Peek();
    if (auto *id = value->CastAsIdentifier(); id && upcoming.type == '=') {
      scanner_->Next();
      return ParseAssignmentRhs(id);
    }
    return value;
  }

  Node *ParseValueOrIdentifier(bool can_be_optional) {
    LOG_ENTER();
    Token t = scanner_->Next();
    switch (t.type) {
    case TokenType::kStringLiteral:
      return StringScalar::FromLiteral(node_arena_, t.text);
    case TokenType::kNumberLiteral: return ParseIntFromToken(t);
    case TokenType::kIdentifier:
      if (scanner_->Peek().type == '(') {
        scanner_->Next();
        return ParseFunCall(t);
      }
      return Make<Identifier>(t.text);
    case TokenType::kOpenSquare: return ParseArrayOrListComprehension();
    case TokenType::kOpenBrace:
      return ParseList(
        Make<List>(List::Type::kMap), [&]() { return ParseMapTuple(); },
        TokenType::kCloseBrace);
    default:  //
      if (!can_be_optional) {
        ErrAt(t) << "expected value of sorts\n";
      }
      return nullptr;
    }
  }

  Node *ParseIfElse(Node *if_branch) {
    LOG_ENTER();
    Token op = scanner_->Next();
    assert(op.type == TokenType::kIf);  // expected this at this point.
    Node *condition = ParseExpression();
    Node *else_branch = nullptr;
    op = scanner_->Peek();
    if (op.type == TokenType::kElse) {
      scanner_->Next();
      else_branch = ParseExpression();
    }
    return Make<Ternary>(condition, if_branch, else_branch);
  }

  Node *ParseExpression(bool can_be_optional = false) {
    LOG_ENTER();
    Node *n;

    switch (scanner_->Peek().type) {
    case '-':
    case TokenType::kNot: {
      Token tok = scanner_->Next();
      n = Make<UnaryExpr>(tok.type, ParseExpression(can_be_optional));
      break;
    }
    case '(': n = ParseParenExpressionOrTuple(); break;
    default: n = ParseValueOrIdentifier(can_be_optional);
    }
    if (n == nullptr) return n;

    const Token upcoming = scanner_->Peek();
    if (upcoming.type == TokenType::kIf) {
      return ParseIfElse(n);
    }

    // TODO: properly handle precdence. Needed once we actually do
    // expression eval. For now: just accept language.
    switch (upcoming.type) {
    case '+':  // Arithmeteic
    case '-':
    case '*':
    case '/':
    case TokenType::kLessThan:  // Relational
    case TokenType::kLessEqual:
    case TokenType::kEqualityComparison:
    case TokenType::kGreaterEqual:
    case TokenType::kGreaterThan:
    case TokenType::kNotEqual:
    case '.':    // scoped invocation
    case '%': {  // format expr.
      Token op = scanner_->Next();
      return Make<BinOpNode>(n, ParseExpression(), op.type);
    }
    default: return n;
    }
  }

  Node *ParseParenExpressionOrTuple() {
    LOG_ENTER();
    Token p = scanner_->Next();
    assert(p.type == '(');  // We have only be called if this is true.
    Node *const exp = ParseExpression(true);  // null if this is an empty tuple
    p = scanner_->Peek();
    if (exp && p.type == ')') {
      scanner_->Next();
      return exp;
    }

    // After the first comma we expect this to be a tuple
    List *tuple = Make<List>(List::kTuple);
    if (!exp) {
      return tuple;
    }
    tuple->Append(node_arena_, exp);

    for (;;) {
      Token separator = scanner_->Next();
      if (separator.type == ')') break;
      if (separator.type != ',') {
        ErrAt(separator) << "expected `,` as tuple separator.\n";
        break;
      }
      if (scanner_->Peek().type == ')') {
        scanner_->Next();  // closing comma at end.
        break;
      }
      tuple->Append(node_arena_, ParseExpression());
    }
    return tuple;
  }

  IntScalar *ParseIntFromToken(Token t) {
    LOG_ENTER();
    IntScalar *scalar = IntScalar::FromLiteral(node_arena_, t.text);
    if (!scalar) {
      ErrAt(t) << "Error parsing int literal\n";
    }
    return scalar;
  }

  BinOpNode *ParseMapTuple() {
    LOG_ENTER();
    Token p = scanner_->Next();
    Node *lhs;
    switch (p.type) {
    case kStringLiteral:
      lhs = StringScalar::FromLiteral(node_arena_, p.text);
      break;
    case kNumberLiteral:  //
      lhs = ParseIntFromToken(p);
      break;
    case kIdentifier:  //
      lhs = Make<Identifier>(p.text);
      break;
    default:  //
      ErrAt(p) << "expected literal value or identifier as map key\n";
      return nullptr;
    }

    p = scanner_->Next();
    if (p.type != ':') {
      ErrAt(p) << "expected `:` in map-tuple\n";
      return nullptr;
    }
    return Make<BinOpNode>(lhs, ParseExpression(), TokenType::kColon);
  }

  Node *ParseArrayOrListComprehension() {
    LOG_ENTER();
    Token upcoming = scanner_->Peek();
    if (upcoming.type == ']') {
      scanner_->Next();
      return Make<List>(List::Type::kList);  // empty list.
    }
    Node *first_expression = ParseExpression();
    if (!first_expression) return nullptr;
    switch (scanner_->Peek().type) {
    case TokenType::kFor: return ParseListComprehension(first_expression);
    case TokenType::kComma: scanner_->Next(); break;
    case TokenType::kCloseSquare:
      // perfectly reasonable
      break;
    default: ErrAt(scanner_->Peek()) << "expected `for`, `]`, or `,`'\n"; break;
    }
    // Alright at this point we know that we have a regular list and the
    // first expression was part of it.
    List *result = Make<List>(List::Type::kList);
    result->Append(node_arena_, first_expression);
    return ParseList(
      result, [&]() { return ParseExpression(); }, TokenType::kCloseSquare);
  }

  Node *ParseListComprehension(Node *start_expression) {
    LOG_ENTER();
    // start_expression `for` ident[,ident...] `in` expression.
    // start_expression already parsed, `for` still in scanner; extract that:
    scanner_->Next();

    // TODO: Here we parsed expressions; maybe just parse Identifiers ?
    List *exp_list = ParseList(
      Make<List>(List::Type::kList), [&]() { return ParseExpression(); },
      TokenType::kIn);
    Node *source = ParseExpression();
    Node *lh = Make<ListComprehension>(start_expression, exp_list, source);
    if (scanner_->Peek().type != ']') {
      ErrAt(scanner_->Peek())
        << "expected closing ']' at end of list comprehension\n";
      return nullptr;
    }
    scanner_->Next();
    return lh;
  }

  std::ostream &ErrAt(Token t) {
    err_out_ << filename_ << ":" << scanner_->line_col().GetRange(t.text)
             << " got '" << t.text << "'; ";
    error_ = true;
    last_token_ = t;
    return err_out_;
  }

  // Error token or kEof
  Token lastToken() const { return last_token_; }
  bool parse_error() const { return error_; }

  // Convenience factory creating in our Arena, forwarding to constructor.
  template <typename T, class... U>
  T *Make(U &&...args) {
    return node_arena_->New<T>(std::forward<U>(args)...);
  }

 private:
  Scanner *const scanner_;
  Arena *const node_arena_;
  const char *filename_;
  std::ostream &err_out_;
  List *previous_parse_result_ = nullptr;
  bool error_ = false;
  Token last_token_;
};

Parser::Parser(Scanner *token_source, Arena *allocator,
               const char *info_filename, std::ostream &err_out)
    : impl_(new Impl(token_source, allocator, info_filename, err_out)) {}
Parser::~Parser() = default;

List *Parser::parse() { return impl_->parse(); }
bool Parser::parse_error() const { return impl_->parse_error(); }
Token Parser::lastToken() const { return impl_->lastToken(); }
}  // namespace bant
