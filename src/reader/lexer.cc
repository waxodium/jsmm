#include "reader/lexer.hh"

#include <array>
#include <unordered_set>

namespace jsmm::reader {
namespace {

const std::unordered_set<std::string> keywords = {
    "async",    "await",    "break",      "case",    "catch", "class",    "const",
    "continue", "debugger", "default",    "delete",  "do",    "else",     "enum",
    "export",   "extends",  "false",      "finally", "for",   "function", "if",
    "import",   "in",       "instanceof", "int",     "let",   "new",      "null",
    "number",   "return",   "signed",   "static",     "super",   "switch", "this",   "throw",
    "try",      "typeof",   "true",     "unsigned",   "var",     "void",  "while",    "with",
    "yield",    "bytes",    "type",    "auto",  "string",   "bool",   "boolean",
    "byte"};

const std::vector<std::string_view> punctuators = {
    ">>>=", "**=", "===", "!==", ">>>", "==", "!=", "<=", ">=", "++", "--", "<<", ">>", "&&", "||",
    "??",   "+=",  "-=",  "*=",  "/=",  "%=", "**", "{",  "}",  "(",  ")",  "[",  "]",  ".",  ";",
    ",",    "<",   ">",   "+",   "-",   "*",  "/",  "%",  "=",  "!",  "~",  "^",  "&",  "|",  ":"};

bool isWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r' || c == '\n';
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isHexDigit(char c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

bool isIdentifierStart(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 0x80;
}

bool isIdentifierContinue(unsigned char c) {
  return isIdentifierStart(c) || isDigit(static_cast<char>(c));
}

void appendUtf8(std::string &out, unsigned long codePoint) {
  if (codePoint <= 0x7F) {
    out.push_back(static_cast<char>(codePoint));
  } else if (codePoint <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else if (codePoint <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  }
}

} // namespace

Lexer::Lexer(std::string_view source) : source_(source) {}

const LexError &Lexer::error() const noexcept { return error_; }

bool Lexer::failed() const noexcept { return failed_; }

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
        setError("unterminated block comment", start);
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
  const SourcePosition start = position();
  const std::size_t startOffset = offset_;
  advance();
  while (!atEnd() && isIdentifierContinue(static_cast<unsigned char>(peek()))) advance();
  const std::string text(source_.substr(startOffset, offset_ - startOffset));
  finish(token, keywords.contains(text) ? TokenKind::Keyword : TokenKind::Identifier, startOffset,
         start);
  return true;
}

bool Lexer::scanDigits(unsigned base, bool requireOne) {
  bool sawDigit = false;
  while (!atEnd()) {
    const char c = peek();
    const bool digit = base == 16  ? isHexDigit(c)
                       : base == 8 ? (c >= '0' && c <= '7')
                       : base == 2 ? (c == '0' || c == '1')
                                   : isDigit(c);
    if (!digit) break;
    sawDigit = true;
    advance();
  }
  if (requireOne && !sawDigit) {
    setError("expected digit in numeric literal", position());
    return false;
  }
  return true;
}

bool Lexer::scanNumber(Token &token) {
  const SourcePosition start = position();
  const std::size_t startOffset = offset_;

  if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X' || peek(1) == 'b' || peek(1) == 'B' ||
                        peek(1) == 'o' || peek(1) == 'O')) {
    const char prefix = peek(1);
    const unsigned base = (prefix == 'x' || prefix == 'X')   ? 16U
                          : (prefix == 'b' || prefix == 'B') ? 2U
                                                             : 8U;
    advance();
    advance();
    if (!scanDigits(base, true)) return false;
    if (isIdentifierContinue(static_cast<unsigned char>(peek()))) {
      setError("invalid character after numeric literal", position());
      return false;
    }
    finish(token, TokenKind::NumberLiteral, startOffset, start);
    return true;
  }

  if (peek() == '.') {
    advance();
    if (!scanDigits(10, true)) return false;
  } else {
    if (!scanDigits(10, true)) return false;
    if (peek() == '.') {
      advance();
      if (!scanDigits(10, false)) return false;
    }
  }

  if (peek() == 'e' || peek() == 'E') {
    advance();
    if (peek() == '+' || peek() == '-') advance();
    if (!scanDigits(10, true)) return false;
  }

  if (isIdentifierStart(static_cast<unsigned char>(peek()))) {
    setError("invalid character after numeric literal", position());
    return false;
  }

  finish(token, TokenKind::NumberLiteral, startOffset, start);
  return true;
}

bool Lexer::scanString(Token &token, char quote) {
  const SourcePosition start = position();
  const std::size_t startOffset = offset_;
  advance();
  std::string decoded;
  for (;;) {
    if (atEnd()) {
      setError("unterminated string literal", start);
      return false;
    }
    const char c = advance();
    if (c == quote) break;
    if (c == '\n' || c == '\r') {
      setError("unescaped line terminator in string literal", start);
      return false;
    }
    if (c != '\\') {
      decoded.push_back(c);
      continue;
    }
    if (atEnd()) {
      setError("unterminated string literal", start);
      return false;
    }
    const char escaped = advance();
    switch (escaped) {
    case 'n': decoded.push_back('\n'); break;
    case 't': decoded.push_back('\t'); break;
    case 'r': decoded.push_back('\r'); break;
    case 'v': decoded.push_back('\v'); break;
    case 'b': decoded.push_back('\b'); break;
    case 'f': decoded.push_back('\f'); break;
    case '0': decoded.push_back('\0'); break;
    case '\\': decoded.push_back('\\'); break;
    case '\'': decoded.push_back('\''); break;
    case '"': decoded.push_back('"'); break;
    case '\r':
      if (peek() == '\n') advance();
      break;
    case '\n': break;
    case 'x': {
      unsigned value = 0;
      for (int i = 0; i < 2; ++i) {
        const char h = advance();
        if (!isHexDigit(h)) {
          setError("invalid \\x escape sequence", position());
          return false;
        }
        value = value * 16U + static_cast<unsigned>(isDigit(h) ? h - '0' : (h | 0x20) - 'a' + 10);
      }
      decoded.push_back(static_cast<char>(value));
      break;
    }
    case 'u': {
      if (peek() == '{') {
        advance();
        unsigned long codePoint = 0;
        int digits = 0;
        while (!atEnd() && peek() != '}') {
          const char h = advance();
          if (!isHexDigit(h)) {
            setError("invalid \\u escape sequence", position());
            return false;
          }
          codePoint = codePoint * 16UL +
                      static_cast<unsigned long>(isDigit(h) ? h - '0' : (h | 0x20) - 'a' + 10);
          if (++digits > 6) {
            setError("invalid \\u escape sequence", position());
            return false;
          }
        }
        if (digits == 0 || !match('}')) {
          setError("invalid \\u escape sequence", position());
          return false;
        }
        appendUtf8(decoded, codePoint);
        break;
      }
      unsigned long codePoint = 0;
      for (int i = 0; i < 4; ++i) {
        if (atEnd() || !isHexDigit(peek())) {
          setError("invalid \\u escape sequence", position());
          return false;
        }
        const char h = advance();
        codePoint = codePoint * 16UL +
                    static_cast<unsigned long>(isDigit(h) ? h - '0' : (h | 0x20) - 'a' + 10);
      }
      appendUtf8(decoded, codePoint);
      break;
    }
    default: setError("invalid escape sequence", position()); return false;
    }
  }
  token.text = std::move(decoded);
  finish(token, TokenKind::StringLiteral, startOffset, start);
  return true;
}

bool Lexer::scanPunctuator(Token &token) {
  const SourcePosition start = position();
  const std::size_t startOffset = offset_;
  for (const auto punctuator : punctuators) {
    if (source_.substr(offset_, punctuator.size()) == punctuator) {
      for (std::size_t i = 0; i < punctuator.size(); ++i) advance();
      finish(token, TokenKind::Punctuator, startOffset, start);
      return true;
    }
  }
  setError("unexpected character", start);
  return false;
}

bool Lexer::atEnd() const noexcept { return offset_ >= source_.size(); }

char Lexer::peek(std::size_t lookahead) const noexcept {
  const std::size_t index = offset_ + lookahead;
  return index < source_.size() ? source_[index] : '\0';
}

char Lexer::advance() noexcept {
  if (atEnd()) return '\0';
  const char c = source_[offset_++];
  if (c == '\r') {
    if (peek() == '\n') ++offset_;
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

SourcePosition Lexer::position() const noexcept { return {static_cast<std::uint32_t>(line_), static_cast<std::uint32_t>(column_)}; }

void Lexer::finish(Token &token, TokenKind kind, std::size_t startOffset,
                   SourcePosition startPosition) {
  token.kind = kind;
  token.lexeme.assign(source_.substr(startOffset, offset_ - startOffset));
  token.start = startPosition;
  token.end = position();
}

} // namespace jsmm::reader
