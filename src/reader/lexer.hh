#pragma once

#include "reader/ast.hh"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace jsmm::reader {

enum class TokenKind { End, Identifier, Keyword, NumberLiteral, StringLiteral, Punctuator };

struct Token {
  TokenKind kind = TokenKind::End;
  std::string lexeme;
  std::string text;
  SourcePosition start{};
  SourcePosition end{};
};

struct LexError {
  std::string message;
  SourcePosition position{};
};

class Lexer final {
public:
  explicit Lexer(std::string_view source);

  std::vector<Token> tokenize();
  [[nodiscard]] const LexError &error() const noexcept;
  [[nodiscard]] bool failed() const noexcept;

private:
  bool scanToken(Token &token);
  bool skipTrivia();
  bool scanIdentifier(Token &token);
  bool scanNumber(Token &token);
  bool scanString(Token &token, char quote);
  bool scanPunctuator(Token &token);
  bool scanDigits(unsigned base, bool requireOne);

  [[nodiscard]] bool atEnd() const noexcept;
  [[nodiscard]] char peek(std::size_t lookahead = 0) const noexcept;
  char advance() noexcept;
  bool match(char expected) noexcept;
  void setError(std::string message, SourcePosition position);
  [[nodiscard]] SourcePosition position() const noexcept;
  void finish(Token &token, TokenKind kind, std::size_t startOffset, SourcePosition startPosition);

  std::string_view source_;
  std::size_t offset_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;
  LexError error_{};
  bool failed_ = false;
};

} // namespace jsmm::reader
