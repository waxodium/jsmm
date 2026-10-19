#include "operations.hh"

#include "engine/object.hh"
#include "compiler/console.hh"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace rma::compiler {

namespace {

double valueToNumberValue(const rma::syntax::Value &value) {
  if (value.isNumber()) return value.asNumber();
  if (value.isBoolean()) return value.asBoolean() ? 1.0 : 0.0;
  if (value.isNull()) return 0.0;
  if (value.isUndefined()) return std::numeric_limits<double>::quiet_NaN();
  if (value.isString()) {
    const std::string text = rma::syntax::utf16ToUtf8(value.asString());
    const auto first = text.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos) return 0.0;
    const auto last = text.find_last_not_of(" \t\n\r\f\v");
    const std::string trimmed = text.substr(first, last - first + 1);
    char *end = nullptr;
    const double result = std::strtod(trimmed.c_str(), &end);
    return end == trimmed.c_str() + trimmed.size() ? result
                                                   : std::numeric_limits<double>::quiet_NaN();
  }
  return std::numeric_limits<double>::quiet_NaN();
}

} // namespace

bool truthy(const rma::syntax::Value &value) {
  if (value.isUndefined() || value.isNull()) return false;
  if (value.isBoolean()) return value.asBoolean();
  if (value.isNumber()) return value.asNumber() != 0.0 && !std::isnan(value.asNumber());
  if (value.isString()) return !value.asString().empty();
  return true;
}

std::string toString(const rma::syntax::Value &value) {
  if (value.isString()) return rma::syntax::utf16ToUtf8(value.asString());
  if (value.isNumber() || value.isBoolean() || value.isNull() || value.isUndefined())
    return formatConsoleValue(value);
  if (value.isObject()) return "[object Object]";
  if (value.isSymbol()) throw std::runtime_error("cannot convert Symbol to string");
  return "undefined";
}

double number(const rma::syntax::Value &value) {
  if (value.isNumber()) return value.asNumber();
  if (value.isBoolean()) return value.asBoolean() ? 1.0 : 0.0;
  if (value.isNull()) return 0.0;
  if (value.isUndefined()) return std::numeric_limits<double>::quiet_NaN();
  if (value.isObject()) return std::numeric_limits<double>::quiet_NaN();
  if (value.isSymbol()) throw std::runtime_error("cannot convert Symbol to number");
  std::string text = toString(value);
  const auto first = text.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string::npos) return 0.0;
  const auto last = text.find_last_not_of(" \t\n\r\f\v");
  text = text.substr(first, last - first + 1);
  char *end = nullptr;
  const double result = std::strtod(text.c_str(), &end);
  if (end != text.c_str() + text.size()) return std::numeric_limits<double>::quiet_NaN();
  return result;
}

std::uint32_t toUint32(const rma::syntax::Value &value) {
  const double n = number(value);
  if (!std::isfinite(n) || n == 0.0) return 0;
  double modulo = std::fmod(std::trunc(n), 4294967296.0);
  if (modulo < 0) modulo += 4294967296.0;
  return static_cast<std::uint32_t>(modulo);
}

std::int32_t toInt32(const rma::syntax::Value &value) {
  const std::uint32_t n = toUint32(value);
  if (n >= 0x80000000U)
    return static_cast<std::int32_t>(static_cast<std::int64_t>(n) - 0x100000000LL);
  return static_cast<std::int32_t>(n);
}

bool strictEqual(const rma::syntax::Value &a, const rma::syntax::Value &b) {
  if (a.storage().index() != b.storage().index()) return false;
  if (a.isUndefined() || a.isNull()) return true;
  if (a.isBoolean()) return a.asBoolean() == b.asBoolean();
  if (a.isNumber()) {
    if (std::isnan(a.asNumber()) || std::isnan(b.asNumber())) return false;
    return a.asNumber() == b.asNumber();
  }
  if (a.isString()) return a.asString() == b.asString();
  if (a.isSymbol()) return a.asSymbol() == b.asSymbol();
  return a.asObject() == b.asObject();
}

bool looseEqual(const rma::syntax::Value &a, const rma::syntax::Value &b) {
  if (a.storage().index() == b.storage().index()) {
    if (a.isUndefined() || a.isNull()) return true;
    if (a.isBoolean()) return a.asBoolean() == b.asBoolean();
    if (a.isNumber()) {
      if (std::isnan(a.asNumber()) || std::isnan(b.asNumber())) return false;
      return a.asNumber() == b.asNumber();
    }
    if (a.isString()) return a.asString() == b.asString();
    if (a.isSymbol()) return a.asSymbol() == b.asSymbol();
    return a.asObject() == b.asObject();
  }
  if (a.isNullish() && b.isNullish()) return true;
  if (a.isNumber() && b.isString()) return looseEqual(a, rma::syntax::Value(valueToNumberValue(b)));
  if (a.isString() && b.isNumber()) return looseEqual(rma::syntax::Value(valueToNumberValue(a)), b);
  if (a.isBoolean()) return looseEqual(rma::syntax::Value(valueToNumberValue(a)), b);
  if (b.isBoolean()) return looseEqual(a, rma::syntax::Value(valueToNumberValue(b)));
  if ((a.isString() || a.isNumber()) && b.isObject())
    return looseEqual(a, rma::syntax::Value(toPrimitive(b)));
  if (a.isObject() && (b.isString() || b.isNumber()))
    return looseEqual(rma::syntax::Value(toPrimitive(a)), b);
  return false;
}

rma::syntax::Value toPrimitive(const rma::syntax::Value &value) {
  if (value.isObject() && value.asObject()) {
    auto obj = value.asObject();
    if (obj->kind == rma::syntax::ObjectKind::Array) {
      auto arr = std::static_pointer_cast<rma::syntax::ArrayObject>(obj);
      std::string result;
      for (std::size_t i = 0; i < arr->elements.size(); ++i) {
        if (i) result += ',';
        result += toString(arr->elements[i]);
      }
      return rma::syntax::Value(result);
    }
  }
  return value;
}

rma::syntax::PropertyKey toPropertyKey(const rma::syntax::Value &value) {
  if (value.isSymbol()) return rma::syntax::PropertyKey(value.asSymbol());
  return rma::syntax::PropertyKey(toString(value));
}

std::string decodeString(const std::string &spelling) {
  if (spelling.size() < 2) throw std::runtime_error("invalid string literal");
  std::string result;
  for (std::size_t i = 1; i + 1 < spelling.size(); ++i) {
    char c = spelling[i];
    if (c != '\\') {
      result += c;
      continue;
    }
    if (++i + 1 > spelling.size()) throw std::runtime_error("invalid string escape");
    switch (spelling[i]) {
    case 'n': result += '\n'; break;
    case 'r': result += '\r'; break;
    case 't': result += '\t'; break;
    case 'b': result += '\b'; break;
    case 'f': result += '\f'; break;
    case 'v': result += '\v'; break;
    case '0': result += '\0'; break;
    case '\\': result += '\\'; break;
    case '\'': result += '\''; break;
    case '"': result += '"'; break;
    default: throw std::runtime_error("unsupported string escape in backend");
    }
  }
  return result;
}

rma::syntax::Value literalValue(const LiteralExpr &literal) {
  switch (literal.kind) {
  case LiteralExpr::Kind::Number: {
    std::string text;
    for (char c : literal.spelling)
      if (c != '_') text += c;
    if (!text.empty() && text.back() == 'n') text.pop_back();
    if (text.size() > 2 && text[0] == '0') {
      const char prefix = static_cast<char>(std::tolower(static_cast<unsigned char>(text[1])));
      if (prefix == 'x' || prefix == 'o' || prefix == 'b') {
        const int base = prefix == 'x' ? 16 : prefix == 'o' ? 8 : 2;
        const auto result = std::stoull(text.substr(2), nullptr, base);
        return rma::syntax::Value(static_cast<double>(result));
      }
    }
    std::size_t used = 0;
    const double result = std::stod(text, &used);
    if (used != text.size())
      throw std::runtime_error("unsupported numeric literal: " + literal.spelling);
    return rma::syntax::Value(result);
  }
  case LiteralExpr::Kind::String: return rma::syntax::Value(decodeString(literal.spelling));
  case LiteralExpr::Kind::Boolean: return rma::syntax::Value(literal.spelling == "true");
  case LiteralExpr::Kind::Null: return rma::syntax::Value::null();
  case LiteralExpr::Kind::Undefined: return rma::syntax::Value::undefined();
  }
  throw std::runtime_error("unknown literal kind");
}

} // namespace rma::compiler
