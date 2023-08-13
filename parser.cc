// next steps
//  triple-string.

#include "parser.h"

#include <fstream>
#include <functional>
#include <iostream>  // for main
#include <ostream>

#include "ast.h"
#include "scanner.h"

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
  List *parse() {
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
      if (tok.type != kIdentifier) {
        ErrAt(tok) << "Expected identifier\n";
        return statement_list;
      }

      // Got identifier, next step: either function call or assignment.
      auto after_id = scanner_->Next();
      switch (after_id.type) {
      case TokenType::kEquals:
        statement_list->Append(node_arena_,
                               ParseAssignmentRhs(Make<Identifier>(tok.text)));
        break;
      case TokenType::kOpenParen:
        statement_list->Append(node_arena_, ParseFunCall(tok));
        break;
      default:
        ErrAt(after_id) << "expected `(` or `=`\n";
        return statement_list;
      }
    }
    return statement_list;
  }

  Assignment *ParseAssignmentRhs(Identifier *id) {
    // '=' already consumed
    return Make<Assignment>(id, ParseExpression());
  }

  FunCall *ParseFunCall(Token identifier) {
    // opening '(' already consumed.
    List *args = ParseList(
      List::Type::kTuple, [&]() { return ValueOrAssignment(); },
      TokenType::kCloseParen);
    return Make<FunCall>(Make<Identifier>(identifier.text), args);
  }

  List *ParseList(List::Type type, const std::function<Node *()> &element_parse,
                  TokenType end_tok) {
    List *result = Make<List>(type);
    Token upcoming = scanner_->Peek();
    while (upcoming.type != end_tok) {
      result->Append(node_arena_, element_parse());
      upcoming = scanner_->Peek();
      if (upcoming.type == ',') {
        scanner_->Next();
        upcoming = scanner_->Peek();
      } else if (upcoming.type != end_tok) {
        ErrAt(scanner_->Next())
          << "Expected `,` or closing `" << end_tok << "`\n";
        return result;
      }
    }
    scanner_->Next();  // eats end_tok
    return result;
  }

  Node *ValueOrAssignment() {
    Node *value = ParseValue();
    if (value->is_identifier() && scanner_->Peek().type == '=') {
      scanner_->Next();
      return ParseAssignmentRhs(static_cast<Identifier *>(value));
    }
    return value;
  }

  Node *ParseValue() {
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
    case TokenType::kOpenSquare:
      return ParseList(
        List::Type::kList, [&]() { return ParseExpression(); },
        TokenType::kCloseSquare);
    case TokenType::kOpenBrace:
      return ParseList(
        List::Type::kMap, [&]() { return ParseMapTuple(); },
        TokenType::kCloseBrace);
    default: ErrAt(t) << "Expected value of sorts\n"; return nullptr;
    }
  }

  Node *ParseExpression() {
    Node *n;
    if (scanner_->Peek().type == '(') {
      n = ParseParenExpressionOrTuple();
    } else {
      n = ParseValue();
    }
    if (n == nullptr) return n;

    const Token upcoming = scanner_->Peek();
    if (upcoming.type == '+' || upcoming.type == '-') {
      Token op = scanner_->Next();
      return Make<BinOpNode>(n, ParseExpression(), op.type);
    } else {
      return n;
    }
  }

  Node *ParseParenExpressionOrTuple() {
    Token p = scanner_->Next();
    assert(p.type == '(');  // We have only be called if this is true.
    Node *exp = ParseExpression();
    p = scanner_->Peek();
    if (p.type == ')') {
      scanner_->Next();
      return exp;
    }

    // After the first comma we expect this to be a tuple
    List *tuple = Make<List>(List::kTuple);
    tuple->Append(node_arena_, exp);

    for (;;) {
      Token separator = scanner_->Next();
      if (separator.type == ')') break;
      if (separator.type != ',') {
        ErrAt(separator) << "',' expected as tuple separator.\n";
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
    IntScalar *scalar = IntScalar::FromLiteral(node_arena_, t.text);
    if (!scalar) {
      ErrAt(t) << "Error parsing int literal\n";
    }
    return scalar;
  }

  BinOpNode *ParseMapTuple() {
    Token p = scanner_->Next();
    Node *lhs;
    switch (p.type) {
    case kStringLiteral:
      lhs = StringScalar::FromLiteral(node_arena_, p.text);
      break;
    case kNumberLiteral:  //
      lhs = ParseIntFromToken(p);
      break;
    default:  //
      ErrAt(p) << "Expected literal value as map key\n";
      return nullptr;
    }

    p = scanner_->Next();
    if (p.type != ':') {
      ErrAt(p) << "Expected `:` in map-tuple\n";
      return nullptr;
    }
    return Make<BinOpNode>(lhs, ParseExpression(), ':');
  }

  std::ostream &ErrAt(Token t) {
    err_out_ << filename_ << ":" << scanner_->GetPos(t.text) << "'" << t.text
             << "' ";
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
Parser::~Parser() {}

List *Parser::parse() { return impl_->parse(); }
bool Parser::parse_error() const { return impl_->parse_error(); }
Token Parser::lastToken() const { return impl_->lastToken(); }
