#include "lexer.hh"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

namespace edon::ecma {
namespace {

const std::unordered_set<std::string> keywords = {
    "await",    "break",      "case",      "catch", "class",    "const",      "continue",
    "debugger", "default",    "delete",    "do",    "else",     "enum",       "export",
    "extends",  "false",      "finally",   "for",   "function", "if",         "import",
    "in",       "instanceof", "new",       "null",  "return",   "super",      "switch",
    "this",     "throw",      "true",      "try",   "typeof",   "var",        "void",
    "while",    "with",       "yield",     "let",   "static",   "implements", "interface",
    "package",  "private",    "protected", "public"};

const std::vector<std::string_view> punctuators = {">>>=",
                                                   "**=",
                                                   "&&=",
                                                   "||=",
                                                   "?"
                                                   "?=",
                                                   ">>>",
                                                   "===",
                                                   "!==",
                                                   ">>=",
                                                   "<<=",
                                                   "...",
                                                   "=>",
                                                   "==",
                                                   "!=",
                                                   "<=",
                                                   ">=",
                                                   "++",
                                                   "--",
                                                   "<<",
                                                   ">>",
                                                   "&&",
                                                   "||",
                                                   "??",
                                                   "?.",
                                                   "+=",
                                                   "-=",
                                                   "*=",
                                                   "/=",
                                                   "%=",
                                                   "&=",
                                                   "|=",
                                                   "^=",
                                                   "**",
                                                   "{",
                                                   "}",
                                                   "(",
                                                   ")",
                                                   "[",
                                                   "]",
                                                   ".",
                                                   ";",
                                                   ",",
                                                   "<",
                                                   ">",
                                                   "+",
                                                   "-"};

bool isWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r' || c == '\n';
}

} // namespace

Lexer::Lexer(std::string_view source) : source_(source) {}

const char *Lexer::tokenKindName(TokenKind kind) noexcept {
  switch (kind) {
  case TokenKind::End: return "end";
  case TokenKind::Identifier: return "identifier";
  case TokenKind::Keyword: return "keyword";
  case TokenKind::NumericLiteral: return "number";
  case TokenKind::BigIntLiteral: return "bigint";
  case TokenKind::StringLiteral: return "string";
  case TokenKind::TemplateLiteral: return "template";
  case TokenKind::Punctuator: return "punctuator";
  }
  return "unknown";
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;
  while (!failed_) {
    Token token;
    if (!scanToken(token)) break;
    tokens.push_back(std::move(token));
    if (tokens.back().kind == TokenKind::End) break;
  }
  return tokens;
}

bool Lexer::scanToken(Token &token) {
  if (!skipTrivia()) return false;
  const SourcePosition start = position();
  const std::size_t startOffset = offset_;
  if (atEnd()) {
    finish(token, TokenKind::End, startOffset, start);
    return true;
  }

  const char c = peek();
  if (isIdentifierStart(static_cast<unsigned char>(c))) return scanIdentifier(token);
  if (isDigit(c) || (c == '.' && isDigit(peek(1)))) return scanNumber(token);
  if (c == '\'' || c == '"') return scanString(token, c);
  if (c == '`') return scanTemplate(token);
  return scanPunctuator(token);
}

bool Lexer::skipTrivia() {
  for (;;) {
    while (!atEnd() && isWhitespace(peek())) advance();
    if (offset_ == 0 && peek() == '#' && peek(1) == '!') {
      while (!atEnd() && peek() != '\n' && peek() != '\r') advance();
      continue;
    }
    if (peek() != '/') return true;
    if (peek(1) == '/') {
      advance();
      advance();
      while (!atEnd() && peek() != '\n' && peek() != '\r') advance();
      continue;
    }
    if (peek(1) == '*') {
      const SourcePosition start = position();
      advance();
      advance();
      while (!atEnd() && !(peek() == '*' && peek(1) == '/')) advance();
      if (atEnd()) {
        setError("Unterminated block comment", start);
        return false;
      }
      advance();
      advance();
      continue;
    }
    return true;
  }
}

bool Lexer::scanIdentifier(Token &token) {
  const auto start = position();
  const auto startOffset = offset_;
  advance();
  while (!atEnd() && isIdentifierContinue(static_cast<unsigned char>(peek()))) advance();
  const std::string text(source_.substr(startOffset, offset_ - startOffset));
  finish(token, keywords.contains(text) ? TokenKind::Keyword : TokenKind::Identifier, startOffset,
         start);
  return true;
}

bool Lexer::scanDigits(unsigned base, bool requireOne, bool separators) {
  bool sawDigit = false;
  bool previousSeparator = false;
  while (!atEnd()) {
    const char c = peek();
    const bool digit = base == 16   ? isHexDigit(c)
                       : base == 10 ? isDigit(c)
                       : base == 8  ? (c >= '0' && c <= '7')
                                    : (c == '0' || c == '1');
    if (digit) {
      sawDigit = true;
      previousSeparator = false;
      advance();
      continue;
    }
    if (c == '_' && separators) {
      if (!sawDigit || previousSeparator || atEnd()) {
        setError("Numeric separator must be between digits", position());
        return false;
      }
      const char next = peek(1);
      const bool nextDigit = base == 16   ? isHexDigit(next)
                             : base == 10 ? isDigit(next)
                             : base == 8  ? (next >= '0' && next <= '7')
                                          : (next == '0' || next == '1');
      if (!nextDigit) {
        setError("Numeric separator must be between digits", position());
        return false;
      }
      previousSeparator = true;
      advance();
      continue;
    }
    break;
  }
  if (requireOne && !sawDigit) {
    setError("Expected digit in numeric literal", position());
    return false;
  }
  return true;
}

bool Lexer::scanNumber(Token &token) {
  const auto start = position();
  const auto startOffset = offset_;
  bool bigint = false;

  if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X' || peek(1) == 'b' || peek(1) == 'B' ||
                        peek(1) == 'o' || peek(1) == 'O')) {
    const char prefix = peek(1);
    const unsigned base = (prefix == 'x' || prefix == 'X')   ? 16U
                          : (prefix == 'b' || prefix == 'B') ? 2U
                                                             : 8U;
    advance();
    advance();
    if (!scanDigits(base, true, true)) return false;
    if (peek() == 'n') {
      bigint = true;
      advance();
    }
    if (isIdentifierContinue(static_cast<unsigned char>(peek()))) {
      setError("Invalid character after numeric literal", position());
      return false;
    }
    finish(token, bigint ? TokenKind::BigIntLiteral : TokenKind::NumericLiteral, startOffset,
           start);
    return true;
  }

  bool hasDot = false;
  if (peek() == '.') {
    hasDot = true;
    advance();
    if (!scanDigits(10, true, true)) return false;
  } else {
    if (!scanDigits(10, true, true)) return false;
    if (peek() == '.') {
      hasDot = true;
      advance();
      if (!scanDigits(10, false, true)) return false;
    }
  }

  if (peek() == 'e' || peek() == 'E') {
    advance();
    if (peek() == '+' || peek() == '-') advance();
    if (!scanDigits(10, true, true)) return false;
  }
  if (peek() == 'n') {
    if (hasDot || source_.substr(startOffset, offset_ - startOffset).find_first_of("eE") !=
                      std::string_view::npos) {
      setError("BigInt literal cannot contain a decimal point or exponent", position());
      return false;
    }
    bigint = true;
    advance();
  }
  if (isIdentifierStart(static_cast<unsigned char>(peek()))) {
    setError("Invalid character after numeric literal", position());
    return false;
  }
  finish(token, bigint ? TokenKind::BigIntLiteral : TokenKind::NumericLiteral, startOffset, start);
  return true;
}

bool Lexer::scanString(Token &token, char quote) {
  const auto start = position();
  const auto startOffset = offset_;
  advance();
  while (!atEnd()) {
    const char c = advance();
    if (c == quote) {
      finish(token, TokenKind::StringLiteral, startOffset, start);
      return true;
    }
    if (c == '\\') {
      if (atEnd()) break;
      const char escaped = advance();
      if (escaped == '\r' && peek() == '\n') advance();
      continue;
    }
    if (c == '\n' || c == '\r') {
      setError("Unescaped line terminator in string literal", start);
      return false;
    }
  }
  setError("Unterminated string literal", start);
  return false;
}

bool Lexer::scanTemplate(Token &token) {
  const auto start = position();
  const auto startOffset = offset_;
  advance();
  while (!atEnd()) {
    const char c = advance();
    if (c == '`') {
      finish(token, TokenKind::TemplateLiteral, startOffset, start);
      return true;
    }
    if (c == '\\') {
      if (!atEnd()) advance();
      continue;
    }
  }
  setError("Unterminated template literal", start);
  return false;
}

bool Lexer::scanPunctuator(Token &token) {
  const auto start = position();
  const auto startOffset = offset_;
  for (const auto punctuator : punctuators) {
    if (punctuator == "?." && peek(2) >= '0' && peek(2) <= '9') continue;
    if (source_.substr(offset_, punctuator.size()) == punctuator) {
      for (std::size_t i = 0; i < punctuator.size(); ++i) advance();
      finish(token, TokenKind::Punctuator, startOffset, start);
      return true;
    }
  }
  static constexpr std::string_view singles = "=!*%&|^~?:+-*/<>.";
  if (singles.find(peek()) != std::string_view::npos) {
    advance();
    finish(token, TokenKind::Punctuator, startOffset, start);
    return true;
  }
  setError("Unexpected character", start);
  return false;
}

bool Lexer::isIdentifierStart(unsigned char c) const noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 0x80;
}

bool Lexer::isIdentifierContinue(unsigned char c) const noexcept {
  return isIdentifierStart(c) || (c >= '0' && c <= '9');
}

bool Lexer::isDigit(char c) const noexcept { return c >= '0' && c <= '9'; }
bool Lexer::isHexDigit(char c) const noexcept {
  return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
bool Lexer::isLineTerminator(char c) const noexcept { return c == '\n' || c == '\r'; }
bool Lexer::atEnd() const noexcept { return offset_ >= source_.size(); }
char Lexer::peek(std::size_t lookahead) const noexcept {
  const auto index = offset_ + lookahead;
  return index < source_.size() ? source_[index] : '\0';
}
char Lexer::advance() noexcept {
  if (atEnd()) return '\0';
  const char c = source_[offset_++];
  if (c == '\r') {
    if (peek() == '\n') { ++offset_; }
    ++line_;
    column_ = 1;
  } else if (c == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return c;
}
bool Lexer::match(char expected) noexcept {
  if (atEnd() || peek() != expected) return false;
  advance();
  return true;
}
void Lexer::setError(std::string message, SourcePosition at) {
  failed_ = true;
  error_ = {std::move(message), at};
}
SourcePosition Lexer::position() const noexcept { return {offset_, line_, column_}; }
void Lexer::finish(Token &token, TokenKind kind, std::size_t startOffset,
                   SourcePosition startPosition) {
  token.kind = kind;
  token.lexeme.assign(source_.substr(startOffset, offset_ - startOffset));
  token.start = startPosition;
  token.end = position();
}

} // namespace edon::ecma
