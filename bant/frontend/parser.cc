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

#include "bant/frontend/parser.h"

#include <functional>
#include <iostream>

#include "bant/frontend/ast.h"
#include "bant/frontend/scanner.h"

// Set to 1 to get a parse tree trace. Not thread safe of course.
#if 0
static int sNodeNum = 0;
static int sIndent = 0;
static bant::Scanner *sScanner = nullptr;
class NestingLogger {
public:
  NestingLogger(const char *fun, const bant::Token &tok) {
    ++sNodeNum;
    ++sIndent;
    std::cerr << sNodeNum << std::string(2 * sIndent, '.') << fun
              << " " << tok << "@" << sScanner->line_col().GetRange(tok.text)
              << "\n";
  }
  ~NestingLogger() {
    --sIndent;
  }
};

#define LOG_ENTER() NestingLogger _log(__FUNCTION__, scanner_->Peek())
#define LOG_INIT(scanner)   \
  {                         \
    sScanner = scanner;     \
    sNodeNum = sIndent = 0; \
  }                         \
  LOG_ENTER()
#else
#define LOG_ENTER()
#define LOG_INIT(ignore)
#endif

namespace bant {
// Simple recursive descent parser. As Parser::Impl to not clobber the header
// file with all the parse methods needed for the productions.
class Parser::Impl {
 public:
  Impl(Scanner *token_source, Arena *allocator, std::string_view info_filename,
       std::ostream &err_out)
      : scanner_(token_source),
        node_arena_(allocator),
        filename_(info_filename),
        err_out_(err_out) {}

  // Parse file. If there is an error, return at least partial tree.
  // A file is a list of data structures, function calls, or assignments..
  List *parse() {
    LOG_INIT(scanner_);
    List *const statement_list = Make<List>(List::Type::kList);
    while (!error_) {
      const Token tok = scanner_->Next();
      if (tok.type == kEof) {
        return statement_list;
      }
      if (tok.type == kStringLiteral) {
        continue;  // Pythonism: Ignoring toplevel document no-effect statement
      }

      if (tok.type == '[') {
        statement_list->Append(
          node_arena_, ParseListOrListComprehension(List::Type::kList, [&]() {
            return ParseExpression();
          }));
        continue;
      }

      // Any other toplevel element is expected to start with an identifier.
      if (tok.type != kIdentifier) {
        ErrAt(tok) << "expected identifier\n";
        return statement_list;
      }

      // Got identifier, next step: either function call or assignment.
      const Token after_id = scanner_->Next();
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

  Node *ExpressionOrAssignment() {
    LOG_ENTER();
    Node *value = ParseExpression();
    if (value == nullptr) return nullptr;
    Token upcoming = scanner_->Peek();
    if (auto *id = value->CastAsIdentifier(); id && upcoming.type == '=') {
      scanner_->Next();
      return ParseAssignmentRhs(id);
    }
    return value;
  }

  // Parse expressions produced by element_parse up to and including end_tok
  // is reached.
  using ListElementParse = std::function<Node *()>;
  List *ParseList(List *result, const ListElementParse &element_parse,
                  TokenType end_tok) {
    LOG_ENTER();
    // Opening list-token (e.g. '[', '(', '{') already consumed.
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
    scanner_->Next();  // consume end_tok
    return result;
  }

  FunCall *ParseFunCall(Token identifier) {
    LOG_ENTER();
    // opening '(' already consumed.
    List *args = ParseList(
      Make<List>(List::Type::kTuple),
      [&]() { return ExpressionOrAssignment(); }, TokenType::kCloseParen);
    return Make<FunCall>(Make<Identifier>(identifier.text), args);
  }

  Node *ParseIfElse(Node *if_branch) {
    LOG_ENTER();
    // 'if' seen, but not consumed yet.
    Token tok = scanner_->Next();
    assert(tok.type == TokenType::kIf);  // expected this at this point.
    Node *condition = ParseExpression();
    Node *else_branch = nullptr;
    tok = scanner_->Peek();
    if (tok.type == TokenType::kElse) {
      scanner_->Next();
      else_branch = ParseExpression();
    }
    return Make<Ternary>(condition, if_branch, else_branch);
  }

  Node *ParseArrayOrSliceAccess() {
    LOG_ENTER();
    // '[' seen, but not consumed yet.
    // array_access = expression ']'
    //              | expression ':' expression ']'
    Node *n = ParseExpression(/*can_be_optional=*/true);
    const Token separator_or_end = scanner_->Next();
    switch (separator_or_end.type) {
    case ']': {
      if (!n) ErrAt(separator_or_end) << "Can not have an empty array access";
      return n;
    }
    case ':': {
      Node *rhs = ParseExpression(/*can_be_optional=*/true);
      const Token end = scanner_->Next();
      if (end.type != ']') {
        ErrAt(end) << "Expected closing ']' of array access\n";
        return nullptr;
      }
      if (n == nullptr && rhs == nullptr) {
        ErrAt(end)
          << "Expected at least one valid expression before or after the ':'\n";
        return nullptr;
      }
      return Make<BinOpNode>(n, rhs, TokenType::kColon);
    }
    default: ErrAt(separator_or_end) << "Expected ':' or ']'\n"; return nullptr;
    }
  }

  Node *ParseValueOrIdentifier(bool can_be_optional) {
    LOG_ENTER();
    const Token t = scanner_->Peek();  // can't consume yet if default hits
    switch (t.type) {
    case TokenType::kStringLiteral:
      return StringScalar::FromLiteral(node_arena_, scanner_->Next().text);
    case TokenType::kNumberLiteral:  //
      return ParseIntFromToken(scanner_->Next());
    case TokenType::kIdentifier:
      scanner_->Next();
      if (scanner_->Peek().type == '(') {
        scanner_->Next();
        return ParseFunCall(t);
      }
      return Make<Identifier>(t.text);
    case TokenType::kOpenSquare:
      scanner_->Next();
      return ParseListOrListComprehension(List::Type::kList,
                                          [&]() { return ParseExpression(); });
    case TokenType::kOpenBrace:
      scanner_->Next();
      return ParseListOrListComprehension(List::Type::kMap,
                                          [&]() { return ParseMapTuple(); });
    default:  //
      // Leaving the token in the scanner.
      if (!can_be_optional) {
        ErrAt(t) << "expected value of sorts\n";
      }
      return nullptr;
    }
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

    for (;;) {  // The array access is the only one who can continue.
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
      case TokenType::kIn:
      case TokenType::kNotIn:
      case TokenType::kAnd:
      case TokenType::kOr:
      case TokenType::kPipeOrBitwiseOr:
      case '.':    // scoped invocation
      case '%': {  // format expr.
        Token op = scanner_->Next();
        return Make<BinOpNode>(n, ParseExpression(), op.type);
      }
      case '[': {
        // This is a bit handwavy. We want to disambiguate an array access
        // from a toplevel array in the next line.
        // The correct way would be to take statement-breaking newlines
        // into account (Pythonism). Not doing that yet.
        // For now: look if the lhs looks like someting an array acces
        // would make sense.
        BinOpNode *const lhs_bin = n->CastAsBinOp();
        if (lhs_bin == nullptr && n->CastAsIdentifier() == nullptr) {
          return n;
        }
        if (lhs_bin != nullptr && lhs_bin->op() != '[') {
          ErrAt(scanner_->Peek())
            << "Expected Identifier or array access left of array access\n";
          return nullptr;
        }
        Token op = scanner_->Next();  // '[' operation.
        n = Make<BinOpNode>(n, ParseArrayOrSliceAccess(), op.type);
        // Suffix expression, maybe there is more. Don't return, continue.
        break;
      }
      default: return n;
      }
    }
  }

  Node *ParseParenExpressionOrTuple() {
    LOG_ENTER();
    // '(' seen, but not consumed yet.
    Token p = scanner_->Next();
    assert(p.type == '(');  // Expected precondition.

    // The following expression can be null if this is an empty tuple
    Node *const exp = ParseExpression(/*can_be_optional=*/true);
    p = scanner_->Peek();
    if (exp && p.type == ')') {
      scanner_->Next();
      return exp;
    }

    // After the first comma we expect this to be a tuple
    List *tuple = Make<List>(List::Type::kTuple);
    if (!exp) {
      p = scanner_->Next();
      if (p.type != ')') {
        ErrAt(p) << "This looks like an empty tuple, but ')' is missing\n";
      }
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
    Node *lhs = ParseExpression();

    const Token separator = scanner_->Next();
    if (separator.type != ':') {
      ErrAt(separator) << "expected `:` in map-tuple\n";
      return nullptr;
    }
    return Make<BinOpNode>(lhs, ParseExpression(), TokenType::kColon);
  }

  // Parse list, use ListElementParse function to parse elements.
  Node *ParseListOrListComprehension(List::Type type,
                                     const ListElementParse &element_parser) {
    LOG_ENTER();
    // We're here when '[', '(', or '{' brace already consumed.
    // corresponding close_token ']', ')' or '}' chosen from type.
    // The token after the first expression distinguishes if this is a list
    // or list comprehension.
    // remaining_node
    //   : close_token                                -> empty list
    //   | expression close_token                     -> one element list
    //   | expression 'for' list_comprehension        -> comprehension
    //   | expression ',' [rest-of-list] close_token  -> longer list
    const TokenType expected_close_token = EndTokenFor(type);
    Token upcoming = scanner_->Peek();
    if (upcoming.type == expected_close_token) {
      scanner_->Next();
      return Make<List>(type);  // empty list/tuple/map
    }
    Node *const first_expression = element_parser();
    if (!first_expression) return nullptr;

    const Token tok = scanner_->Peek();
    switch (tok.type) {
    case TokenType::kFor:  //
      return ParseListComprehension(type, first_expression);
    case TokenType::kComma:  //
      scanner_->Next();
      break;
    default:
      if (tok.type != expected_close_token) {
        ErrAt(scanner_->Peek())
          << "expected `for`, `" << expected_close_token << "', or `,`'\n";
      }
      // if it is expected token: good, one-element list.
    }

    // Alright at this point we know that we have a regular list and the
    // first expression was part of it.
    List *result = Make<List>(type);
    result->Append(node_arena_, first_expression);
    return ParseList(result, element_parser, expected_close_token);
  }

  // Parse next thing but only if it is an identifier.
  Identifier *ParseOptionalIdentifier() {
    LOG_ENTER();
    if (scanner_->Peek().type == TokenType::kIdentifier) {
      Token tok = scanner_->Next();
      return Make<Identifier>(tok.text);
    }
    return nullptr;
  }

  // Read for-in constructs until we hit expected_end_token.
  // Creates a left-recursive tree of 'for' BinOpNodes in which the thing
  // to iterate over is on the left, and the variable-tuple in-expression
  // with content on the right. Nested loops have a 'for' loop on their left.
  BinOpNode *ParseComprehensionFor(Node *iterate_target,
                                   TokenType expected_end_token) {
    BinOpNode *for_tree = nullptr;

    // 'for' seen, but not consumed yet.
    while (scanner_->Peek().type == TokenType::kFor) {
      scanner_->Next();
      List *variable_tuple = nullptr;
      // There can be multipole variables in the list, so they are a tuple.

      // On the input, this can look like a list i, i, j or as tuple (i, j, k).
      // We deal with these variants, but in any case, they are follwed by 'in'.
      if (scanner_->Peek().type == '(') {  // (i, j, k) case.
        scanner_->Next();                  // Consume open tuple '('
        variable_tuple = ParseList(        // .. parse until we see close ')'
          Make<List>(List::Type::kTuple),
          [&]() { return ParseOptionalIdentifier(); }, TokenType::kCloseParen);
        const Token expected_in = scanner_->Next();
        if (expected_in.type != TokenType::kIn) {
          ErrAt(expected_in) << "expected 'in' after variable tuple\n";
        }
      } else {  // i, j, k case. Here the expected list end token is 'in'
        variable_tuple = ParseList(
          Make<List>(List::Type::kTuple),
          [&]() { return ParseOptionalIdentifier(); }, TokenType::kIn);
      }
      BinOpNode *const range =
        Make<BinOpNode>(variable_tuple, ParseExpression(), TokenType::kIn);
      for_tree = Make<BinOpNode>(iterate_target, range, TokenType::kFor);
      iterate_target = for_tree;  // Nested loops.
    }

    const Token end_tok = scanner_->Next();
    if (end_tok.type != expected_end_token) {
      ErrAt(end_tok) << "expected " << expected_end_token
                     << " at end of comprehension\n";
      return nullptr;
    }
    return for_tree;
  }

  static TokenType EndTokenFor(List::Type type) {
    switch (type) {
    case List::Type::kList: return TokenType::kCloseSquare;
    case List::Type::kTuple: return TokenType::kCloseParen;
    case List::Type::kMap: return TokenType::kCloseBrace;
    }
    return TokenType::kCloseSquare;  // Should not happen.
  }

  Node *ParseListComprehension(List::Type type, Node *start_expression) {
    LOG_ENTER();
    // start_expression already parsed, 'for' seen, but not consumed.
    //
    // 'for' identifier (,identifier)* ,? 'in' expression.
    return Make<ListComprehension>(
      type, ParseComprehensionFor(start_expression, EndTokenFor(type)));
  }

  std::ostream &ErrAt(Token t) {
    err_out_ << filename_ << ":" << scanner_->line_col().GetRange(t.text)
             << " got '" << t.text << "'; ";
    error_ = true;
    return err_out_;
  }

  bool parse_error() const { return error_; }

  // Convenience factory creating in our Arena, forwarding to constructor.
  template <typename T, class... U>
  T *Make(U &&...args) {
    return node_arena_->New<T>(std::forward<U>(args)...);
  }

 private:
  Scanner *const scanner_;
  Arena *const node_arena_;
  const std::string_view filename_;
  std::ostream &err_out_;
  bool error_ = false;
};

Parser::Parser(Scanner *token_source, Arena *allocator,
               std::string_view info_filename, std::ostream &err_out)
    : impl_(new Impl(token_source, allocator, info_filename, err_out)) {}
Parser::~Parser() = default;

List *Parser::parse() { return impl_->parse(); }
bool Parser::parse_error() const { return impl_->parse_error(); }
}  // namespace bant
