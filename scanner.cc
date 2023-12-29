
#include "scanner.h"

#include <algorithm>
#include <cassert>
#include <string_view>

std::ostream &operator<<(std::ostream &o, TokenType t) {
  switch (t) {
  case '(':
  case ')':
  case '{':
  case '}':
  case '[':
  case ']':
  case ':':
  case ',':
  case '=':
  case '+':
  case '-':
  case '.':
  case '%': o << (char)t; break;

  case TokenType::kIdentifier: o << "ident"; break;
  case TokenType::kStringLiteral: o << "string"; break;
  case TokenType::kNumberLiteral: o << "number"; break;
  case TokenType::kFor: o << "for"; break;
  case TokenType::kIn: o << "in"; break;
  case TokenType::kError: o << "<<ERROR>>"; break;
  case TokenType::kEof: o << "<<EOF>>"; break;
  }
  return o;
}

std::ostream &operator<<(std::ostream &o, const Token t) {
  o << t.type << "('" << t.text << "')";
  return o;
}

Scanner::Scanner(std::string_view content)
    : content_(content), pos_(content_.begin()) {
  line_map_.push_back(pos_);
}

Scanner::Iterator Scanner::SkipSpace() {
  bool in_comment = false;
  while (pos_ < content_.end() &&
         (isspace(*pos_) || *pos_ == '#' || in_comment)) {
    if (*pos_ == '#') {
      in_comment = true;
    }
    if (*pos_ == '\n') {
      line_map_.push_back(pos_ + 1);
      in_comment = false;
    }
    pos_++;
  }
  return pos_;
}

static bool IsIdentifierChar(char c) {
  return isdigit(c) || isalpha(c) || c == '_';
}

Token Scanner::HandleIdentifierKeywordOrInvalid() {
  const Iterator start = pos_;
  // Digit already ruled out at this point as first character.
  if (!IsIdentifierChar(*start)) {
    ++pos_;
    return {TokenType::kError, {start, 1}};
  }
  while (pos_ < content_.end() && IsIdentifierChar(*pos_)) {
    ++pos_;
  }
  const std::string_view text{start, (size_t)(pos_ - start)};

  // Keywords, anything else will be an identifier.
  if (text == "in") return {TokenType::kIn, text};
  if (text == "for") return {TokenType::kFor, text};
  return {TokenType::kIdentifier, text};
}

Token Scanner::HandleString() {
  bool triple_quote = false;
  const Iterator start = pos_;
  const char str_quote = *pos_;
  pos_++;
  if (pos_ + 1 < content_.end() && *pos_ == str_quote && *(pos_ + 1) == str_quote) {
    triple_quote = true;
    pos_ += 2;
  }
  int close_quote_count = triple_quote ? 3 : 1;
  bool last_was_escape = false;
  while (pos_ < content_.end()) {
    if (*pos_ == str_quote && !last_was_escape) {
      --close_quote_count;
      if (close_quote_count == 0) break;
    } else {
      close_quote_count = triple_quote ? 3 : 1;
    }
    last_was_escape = (*pos_ == '\\');
    ++pos_;
  }
  if (pos_ >= content_.end()) {
    return {TokenType::kError, {start, (size_t)(pos_ - start)}};
  }
  ++pos_;
  return {TokenType::kStringLiteral, {start, (size_t)(pos_ - start)}};
}

Token Scanner::HandleNumber() {
  const Iterator start = pos_;
  bool dot_seen = false;
  ++pos_;
  while (pos_ < content_.end() && (isdigit(*pos_) || *pos_ == '.')) {
    if (*pos_ == '.') {
      if (dot_seen) {
        return {TokenType::kError, {start, (size_t)(pos_ - start)}};
      }
      dot_seen = true;
    }
    ++pos_;
  }
  return {TokenType::kNumberLiteral, {start, (size_t)(pos_ - start)}};
}

Token Scanner::Next() {
  if (SkipSpace() == content_.end()) {
    return {TokenType::kEof, {content_.end(), 0}};
  }
  Token result;
  switch (*pos_) {
  case '(':
  case ')':
  case '{':
  case '}':
  case '[':
  case ']':
  case ',':
  case ':':
  case '=':
  case '+':
  case '-':
  case '.':
  case '%':
    result = {/*.type =*/(TokenType)*pos_, /*.text =*/{pos_, 1}};
    ++pos_;
    break;

  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9': result = HandleNumber(); break;

  case '"':
  case '\'':
    result = HandleString();
    break;

  default: result = HandleIdentifierKeywordOrInvalid(); break;
  }
  return result;
}

std::ostream &operator<<(std::ostream &o, Pos p) {
  o << (p.line + 1) << ":" << (p.col + 1) << ":";
  return o;
}

Pos Scanner::GetPos(std::string_view text) const {
  if (text.begin() < content_.begin() || text.end() > content_.end()) {
    return {0, 0};
  }
  LineMap::const_iterator start =
    std::upper_bound(line_map_.begin(), line_map_.end(), text.begin());
  assert(start - line_map_.begin() > 0);
  --start;
  Pos result;
  result.line = start - line_map_.begin();
  result.col = text.begin() - *start;
  return result;
}
