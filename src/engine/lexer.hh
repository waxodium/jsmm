#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace edon::ecma {

enum class TokenKind {
  End,
  Identifier,
  Keyword,
  NumericLiteral,
  BigIntLiteral,
  StringLiteral,
  TemplateLiteral,
  Punctuator
};

struct SourcePosition {
  std::size_t offset = 0;
  std::size_t line = 1;
  std::size_t column = 1;
};

struct Token {
  TokenKind kind = TokenKind::End;
  std::string lexeme;
  SourcePosition start;
  SourcePosition end;
};

struct LexError {
  std::string message;
  SourcePosition position;
};

class Lexer final {
public:
  explicit Lexer(std::string_view source);

  std::vector<Token> tokenize();
  const LexError &error() const noexcept { return error_; }
  bool failed() const noexcept { return failed_; }

  static const char *tokenKindName(TokenKind kind) noexcept;

private:
  bool scanToken(Token &token);
  bool skipTrivia();
  bool scanIdentifier(Token &token);
  bool scanNumber(Token &token);
  bool scanString(Token &token, char quote);
  bool scanTemplate(Token &token);
  bool scanPunctuator(Token &token);

  bool scanDigits(unsigned base, bool requireOne, bool separators);
  bool isIdentifierStart(unsigned char c) const noexcept;
  bool isIdentifierContinue(unsigned char c) const noexcept;
  bool isDigit(char c) const noexcept;
  bool isHexDigit(char c) const noexcept;
  bool isLineTerminator(char c) const noexcept;
  bool atEnd() const noexcept;
  char peek(std::size_t lookahead = 0) const noexcept;
  char advance() noexcept;
  bool match(char expected) noexcept;
  void setError(std::string message, SourcePosition position);
  SourcePosition position() const noexcept;
  void finish(Token &token, TokenKind kind, std::size_t startOffset, SourcePosition startPosition);

  std::string_view source_;
  std::size_t offset_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;
  LexError error_;
  bool failed_ = false;
};

} // namespace edon::ecma
