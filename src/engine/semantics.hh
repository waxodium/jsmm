#pragma once

#include "value.hh"
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace edon::ecma {

class Semantics {
public:
  static bool toBoolean(const Value &value) {
    if (value.isUndefined() || value.isNull()) return false;
    if (value.isBoolean()) return value.asBoolean();
    if (value.isNumber()) return value.asNumber() != 0.0 && !std::isnan(value.asNumber());
    if (value.isString()) return !value.asString().empty();
    return true;
  }

  static std::string typeOf(const Value &value) {
    if (value.isUndefined()) return "undefined";
    if (value.isNull()) return "object";
    if (value.isBoolean()) return "boolean";
    if (value.isNumber()) return "number";
    if (value.isString()) return "string";
    if (value.isSymbol()) return "symbol";
    return "object";
  }

  static double toNumber(const Value &value) {
    if (value.isUndefined()) return std::numeric_limits<double>::quiet_NaN();
    if (value.isNull()) return 0.0;
    if (value.isBoolean()) return value.asBoolean() ? 1.0 : 0.0;
    if (value.isNumber()) return value.asNumber();
    if (value.isString()) return stringToNumber(value.asString());
    return std::numeric_limits<double>::quiet_NaN();
  }

  static bool strictEqual(const Value &a, const Value &b) {
    if (a.storage().index() != b.storage().index()) return false;
    if (a.isUndefined() || a.isNull()) return true;
    if (a.isBoolean()) return a.asBoolean() == b.asBoolean();
    if (a.isNumber())
      return !std::isnan(a.asNumber()) && !std::isnan(b.asNumber()) && a.asNumber() == b.asNumber();
    if (a.isString()) return a.asString() == b.asString();
    if (a.isSymbol()) return a.asSymbol() == b.asSymbol();
    return a.asObject() == b.asObject();
  }

private:
  static double stringToNumber(const std::string &text) {
    const auto first = text.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos) return 0.0;
    const auto last = text.find_last_not_of(" \t\n\r\f\v");
    const std::string trimmed = text.substr(first, last - first + 1);
    char *end = nullptr;
    const double number = std::strtod(trimmed.c_str(), &end);
    return end == trimmed.c_str() + trimmed.size() ? number
                                                   : std::numeric_limits<double>::quiet_NaN();
  }
};

} // namespace edon::ecma
