#include "parser.hh"
#include "console.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace rma::compiler {
using edon::ecma::Token;
using edon::ecma::TokenKind;
using edon::ecma::Value;

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token &Parser::current() const { return tokens_.at(index_); }
bool Parser::eat(const std::string &lexeme) {
  if (current().lexeme != lexeme) return false;
  ++index_;
  return true;
}
const Token &Parser::consume() {
  const Token &token = current();
  if (token.kind == TokenKind::End) fail("unexpected end of input");
  ++index_;
  return token;
}
[[noreturn]] void Parser::fail(const std::string &message) const {
  throw std::runtime_error("line " + std::to_string(current().start.line) + ", column " +
                           std::to_string(current().start.column) + ": " + message);
}
void Parser::optionalSemicolon() { (void)eat(";"); }

Value Parser::parsePrimary() {
  if (eat("(")) {
    Value value = parseExpression();
    if (!eat(")")) fail("expected ')' after expression");
    return value;
  }

  const Token token = consume();
  if (token.kind == TokenKind::StringLiteral) {
    const std::string &raw = token.lexeme;
    std::u16string value;
    const std::size_t limit = raw.size() - 1;
    const auto hexValue = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    const auto readHex = [&](std::size_t &i, std::size_t digits) -> std::uint32_t {
      if (i + digits > limit) fail("incomplete hexadecimal escape");
      std::uint32_t result = 0;
      for (std::size_t n = 0; n < digits; ++n) {
        const int digit = hexValue(raw[i++]);
        if (digit < 0) fail("invalid hexadecimal escape");
        result = (result << 4) | static_cast<std::uint32_t>(digit);
      }
      return result;
    };
    for (std::size_t i = 1; i < limit;) {
      if (raw[i] != '\\') {
        const std::size_t start = i;
        while (i < limit && raw[i] != '\\') ++i;
        value += edon::ecma::utf8ToUtf16(raw.substr(start, i - start));
        continue;
      }
      ++i;
      if (i >= limit) fail("incomplete string escape");
      const char escaped = raw[i++];
      switch (escaped) {
      case 'n': value.push_back(u'\n'); break;
      case 'r': value.push_back(u'\r'); break;
      case 't': value.push_back(u'\t'); break;
      case 'b': value.push_back(u'\b'); break;
      case 'f': value.push_back(u'\f'); break;
      case 'v': value.push_back(u'\v'); break;
      case '\\': value.push_back(u'\\'); break;
      case '\'': value.push_back(u'\''); break;
      case '"': value.push_back(u'"'); break;
      case '0': value.push_back(u'\0'); break;
      case 'x': value.push_back(static_cast<char16_t>(readHex(i, 2))); break;
      case 'u': {
        std::uint32_t cp = 0;
        if (i < limit && raw[i] == '{') {
          ++i;
          const std::size_t start = i;
          while (i < limit && raw[i] != '}') {
            const int digit = hexValue(raw[i]);
            if (digit < 0 || i - start >= 6) fail("invalid Unicode code point escape");
            cp = (cp << 4) | static_cast<std::uint32_t>(digit);
            ++i;
          }
          if (i == start || i >= limit || raw[i] != '}' || cp > 0x10FFFF ||
              (cp >= 0xD800 && cp <= 0xDFFF))
            fail("invalid Unicode code point escape");
          ++i;
          if (cp <= 0xFFFF) value.push_back(static_cast<char16_t>(cp));
          else {
            cp -= 0x10000;
            value.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            value.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
          }
        } else {
          cp = readHex(i, 4);
          value.push_back(static_cast<char16_t>(cp));
        }
        break;
      }
      case '\n': break;
      case '\r':
        if (i < limit && raw[i] == '\n') ++i;
        break;
      default: {
        const auto lead = static_cast<unsigned char>(escaped);
        const std::size_t count = lead < 0x80             ? 1
                                  : (lead & 0xE0) == 0xC0 ? 2
                                  : (lead & 0xF0) == 0xE0 ? 3
                                  : (lead & 0xF8) == 0xF0 ? 4
                                                          : 1;
        const std::size_t start = i - 1;
        const std::size_t available = std::min(count, limit - start);
        value += edon::ecma::utf8ToUtf16(raw.substr(start, available));
        i = start + available;
        break;
      }
      }
    }
    return Value(std::move(value));
  }
  if (token.kind == TokenKind::NumericLiteral) {
    std::string normalized;
    for (char c : token.lexeme)
      if (c != '_') normalized += c;
    char *end = nullptr;
    const double number = std::strtod(normalized.c_str(), &end);
    if (end != normalized.c_str() + normalized.size() || !std::isfinite(number))
      fail("unsupported numeric literal");
    return Value(number);
  }
  if (token.lexeme == "true") return Value(true);
  if (token.lexeme == "false") return Value(false);
  if (token.lexeme == "null") return Value::null();
  if (token.lexeme == "undefined") return Value::undefined();
  if (token.kind == TokenKind::Identifier) {
    const auto found = bindings_.find(token.lexeme);
    if (found == bindings_.end()) fail("undefined identifier '" + token.lexeme + "'");
    return found->second.value;
  }
  fail("expected expression");
}

Value Parser::parseUnary() {
  if (eat("+")) {
    Value value = parseUnary();
    if (!value.isNumber()) fail("unary '+' currently requires a number");
    return value;
  }
  if (eat("-")) {
    Value value = parseUnary();
    if (!value.isNumber()) fail("unary '-' requires a number");
    return Value(-value.asNumber());
  }
  return parsePrimary();
}

int Parser::precedence(const std::string &op) const {
  if (op == "**") return 7;
  if (op == "*" || op == "/" || op == "%") return 6;
  if (op == "+" || op == "-") return 5;
  if (op == "<<" || op == ">>" || op == ">>>") return 4;
  if (op == "&") return 3;
  if (op == "^") return 2;
  if (op == "|") return 1;
  return -1;
}

Value Parser::applyBinary(const std::string &op, const Value &left, const Value &right) {
  if (op == "+" && (left.isString() || right.isString())) {
    const std::u16string a =
        left.isString() ? left.asString() : edon::ecma::utf8ToUtf16(formatConsoleValue(left));
    const std::u16string b =
        right.isString() ? right.asString() : edon::ecma::utf8ToUtf16(formatConsoleValue(right));
    return Value(a + b);
  }
  if (!left.isNumber() || !right.isNumber())
    fail("operator '" + op + "' requires numeric operands");
  const double a = left.asNumber();
  const double b = right.asNumber();
  if (op == "+") return Value(a + b);
  if (op == "-") return Value(a - b);
  if (op == "*") return Value(a * b);
  if (op == "/") return Value(a / b);
  if (op == "%") return Value(std::fmod(a, b));
  if (op == "**") return Value(std::pow(a, b));
  if (op == "<<")
    return Value(
        static_cast<double>(static_cast<std::int32_t>(a) << (static_cast<unsigned>(b) & 31U)));
  if (op == ">>")
    return Value(
        static_cast<double>(static_cast<std::int32_t>(a) >> (static_cast<unsigned>(b) & 31U)));
  if (op == ">>>")
    return Value(static_cast<double>(static_cast<std::uint32_t>(static_cast<std::int32_t>(a)) >>
                                     (static_cast<unsigned>(b) & 31U)));
  if (op == "&")
    return Value(static_cast<double>(static_cast<std::int32_t>(a) & static_cast<std::int32_t>(b)));
  if (op == "^")
    return Value(static_cast<double>(static_cast<std::int32_t>(a) ^ static_cast<std::int32_t>(b)));
  if (op == "|")
    return Value(static_cast<double>(static_cast<std::int32_t>(a) | static_cast<std::int32_t>(b)));
  fail("unsupported arithmetic operator '" + op + "'");
}

Value Parser::parseExpression(int minimumPrecedence) {
  Value left = parseUnary();
  while (true) {
    const std::string op = current().lexeme;
    const int opPrecedence = precedence(op);
    if (opPrecedence < minimumPrecedence) break;
    consume();
    const int nextMinimum = op == "**" ? opPrecedence : opPrecedence + 1;
    Value right = parseExpression(nextMinimum);
    left = applyBinary(op, left, right);
  }
  return left;
}

void Parser::parseDeclaration() {
  const std::string declaration = consume().lexeme;
  const Token name = consume();
  if (name.kind != TokenKind::Identifier) fail("expected binding name");
  if (bindings_.contains(name.lexeme)) fail("duplicate binding '" + name.lexeme + "'");
  if (!eat("=")) fail("declaration initializer is required in this compiler stage");
  Value value = parseExpression();
  bindings_.emplace(name.lexeme, Binding{std::move(value), declaration != "const"});
  optionalSemicolon();
}

void Parser::parseAssignment() {
  const Token name = consume();
  const auto found = bindings_.find(name.lexeme);
  if (found == bindings_.end()) fail("assignment to undefined identifier '" + name.lexeme + "'");
  if (!found->second.mutableBinding) fail("assignment to constant '" + name.lexeme + "'");
  if (!eat("=")) fail("expected '=' in assignment");
  found->second.value = parseExpression();
  optionalSemicolon();
}

void Parser::parseConsoleCall() {
  if (!eat(".")) fail("expected '.' after console");
  const Token method = consume();
  if (method.kind != TokenKind::Identifier && method.kind != TokenKind::Keyword)
    fail("expected console method name");
  int fd = 1;
  if (!ConsoleBuiltins::resolve(method.lexeme, fd))
    fail("unsupported console method '" + method.lexeme + "'");
  if (!eat("(")) fail("expected '(' after console method");
  PrintStatement statement;
  statement.fd = fd;
  if (!eat(")")) {
    do { statement.arguments.push_back(parseExpression()); } while (eat(","));
    if (!eat(")")) fail("expected ')' after console arguments");
  }
  program_.output.push_back(std::move(statement));
  optionalSemicolon();
}

Program Parser::parse() {
  while (current().kind != TokenKind::End) {
    if (current().lexeme == "const" || current().lexeme == "let" || current().lexeme == "var") {
      parseDeclaration();
    } else if (eat("console")) {
      parseConsoleCall();
    } else if (current().kind == TokenKind::Identifier && index_ + 1 < tokens_.size() &&
               tokens_[index_ + 1].lexeme == "=") {
      parseAssignment();
    } else {
      fail("unsupported statement; expected declaration, assignment, or console call");
    }
  }
  return std::move(program_);
}

} // namespace rma::compiler
