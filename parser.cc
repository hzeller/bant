// next steps
//  triple-string.

#include <ostream>
#include <iostream> // for main
#include <fstream>
#include <functional>

#include "scanner.h"

#define RET_ON_ERROR(token)                                                    \
  ({                                                                           \
    const Token &_t = (token);                                                 \
    if (_t.type == TokenType::kError) {                                        \
      return _t;                                                               \
    }                                                                          \
    _t;                                                                        \
  })

class Parser {
public:
  Parser(const char *filename, Scanner *scanner)
    : filename_(filename), scanner_(scanner) {}

  Token parse_top() {
    for (;;) {
      auto tok = scanner_->Next();
      if (tok.type == kEof) {
        return tok;
      }
      if (tok.type == kStringLiteral) {
        continue;  // crazy Python folks.
      }
      if (tok.type != kIdentifier) {
        MsgAt(tok) << "Expected identifier, got " << tok.text << "\n";
        return ConvertToError(tok);
      }

      // Got identifier, next step: either function call or assignment.
      auto after_id = scanner_->Next();
      switch (after_id.type) {
      case TokenType::kEquals:
        RET_ON_ERROR(ParseAssignmentRhs(tok, "toplevel"));
        break;
      case TokenType::kOpenParen:
        RET_ON_ERROR(ParseFunCall(tok));
        break;
      default:
        MsgAt(after_id)
          << "expected '(' or '=', got " << after_id.text << "\n";
        return ConvertToError(after_id);
      }
    }
  }

  Token ParseAssignmentRhs(Token identifier, const char *msg) {
    // '=' already consumed
    Token t = RET_ON_ERROR(ParseExpression());
    MsgAt(t) << "Got successful assignment for '" << identifier.text << "=' "
             << msg << "\n";
    return t;
  }

  Token ParseFunCall(Token identifier) {
    // open paren already consumed
    Token t = RET_ON_ERROR(ParseList([&]() { return ValueOrAssignment(); },
                                     TokenType::kCloseParen));
    MsgAt(identifier) << "Finished fun call\n";
    return t;
  }

  Token ParseList(const std::function<Token()> &element_parse,
                  TokenType end_tok) {
    Token upcoming = scanner_->Peek();
    while (upcoming.type != end_tok) {
      RET_ON_ERROR(element_parse());
      upcoming = scanner_->Peek();
      if (upcoming.type == ',') {
        scanner_->Next();
        upcoming = scanner_->Peek();
      } else if (upcoming.type != end_tok) {
        MsgAt(upcoming) << "Expected comma or close " << end_tok << "\n";
        return ConvertToError(scanner_->Next());
      }
    }
    return scanner_->Next();  // eats end_tok
  }

  Token ValueOrAssignment() {
    Token t = RET_ON_ERROR(ParseValue());  // simple lhs
    if (t.type == TokenType::kIdentifier && scanner_->Peek().type == '=') {
      scanner_->Next();
      return ParseAssignmentRhs(t, "in fun-call");
    }
    MsgAt(t) << "Value in fun-call\n";
    return t;
  }

  Token ParseValue() {
    Token t = scanner_->Next();
    switch (t.type) {
    case TokenType::kStringLiteral:
    case TokenType::kNumberLiteral:
      return t;
    case TokenType::kIdentifier:
      if (scanner_->Peek().type == '(') {
        scanner_->Next();
          return ParseFunCall(t);
        } else {
          return t;
        }
      case TokenType::kOpenSquare:
        return ParseList([&]() { return ParseExpression(); }, TokenType::kCloseSquare);
      case TokenType::kOpenBrace:
        return ParseList([&]() { return ParseMapTuple(); }, TokenType::kCloseBrace);
      default:
        MsgAt(t) << "Expected value of sorts\n";
        return ConvertToError(t);
      }
  }

  Token ParseExpression(bool tuple_expression_allowed = false) {
    for (;;) {
      Token t;
      if (scanner_->Peek().type == '(') {
        t = ParseParenExpression();
      } else {
        t = ParseValue();
      }
      RET_ON_ERROR(t);
      const Token upcoming = scanner_->Peek();
      if (upcoming.type == '+' || upcoming.type == '-' || upcoming.type == '.'
          || (upcoming.type == ',' && tuple_expression_allowed)) {
        scanner_->Next();
      } else {
        return t;
      }
    }
  }

  Token ParseParenExpression() {
    Token p = scanner_->Next();
    if (p.type != '(') return ConvertToError(p);
    Token exp = ParseExpression(true);
    p = scanner_->Next();
    if (p.type != ')') {
      MsgAt(p) << "Expected close parenthesis\n";
      return ConvertToError(p);
    }
    return exp;
  }

  Token ParseMapTuple() {
    Token p = scanner_->Next();
    if (p.type != kStringLiteral && p.type != kNumberLiteral) {
      MsgAt(p) << "Expected literal in map key\n";
      return ConvertToError(p);
    }
    p = scanner_->Next();
    if (p.type != ':') {
      MsgAt(p) << "Expected ':' in map-tuple\n";
      return ConvertToError(p);
    }
    return ParseExpression();
  }

  std::ostream &MsgAt(Token t) {
    std::cerr << filename_ << ":" << scanner_->GetPos(t.text) << "'" << t.text
              << "' ";
    return std::cerr;
  }

  Token ConvertToError(Token t) {
    t.type = TokenType::kError;
    return t;  // keep text.
  }

private:
  const char *filename_;
  Scanner *const scanner_;
};

std::optional<std::string> ReadFileToString(const char *filename) {
  std::ifstream is(filename, std::ifstream::binary);
  if (!is.good()) return std::nullopt;
  std::string result;
  char buffer[4096];
  for (;;) {
    is.read(buffer, sizeof(buffer));
    result.append(buffer, is.gcount());
    if (!is.good()) break;
  }
  return result;
}

static int usage(const char *prog) {
  fprintf(stderr, "Usage: %s <filename> [<filename>...]\n", prog);
  return 1;
}

int main(int argc, char *argv[]) {
  if (argc <= 1) return usage(argv[0]);
  int file_count = 0;
  int file_error_count = 0;

  for (int i = 1; i < argc; ++i) {
    const char *const filename = argv[i];
    std::optional<std::string> content = ReadFileToString(filename);
    if (!content.has_value()) {
      std::cerr << "Could not read " << filename << "\n";
      ++file_error_count;
      continue;
    }
    ++file_count;
    Scanner scanner(*content);
    Parser parser(filename, &scanner);
    Token last = parser.parse_top();
    if (last.type != kEof) {
      std::cout << filename << ":" << scanner.GetPos(last.text) <<
        ": FAILED AT '" << last.text << "' ----------------- \n";
      ++file_error_count;
    }
  }

  fprintf(stderr, "Scanned %d files; failed to read %d files\n",
          file_count, file_error_count);

  return file_error_count;
}
