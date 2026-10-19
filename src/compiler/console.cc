#include "console.hh"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace rma::compiler {
using namespace rma::syntax;

ObjectPtr ConsoleBuiltins::namespaceObject() {
  auto console = std::make_shared<Object>();
  const auto add = [&console](const char *name, int fd) {
    ObjectOperations::defineDataProperty(console, PropertyKey(name),
                                         Value(fd == 1 ? "stdout" : "stderr"), false, true, false);
  };
  add("log", 1);
  add("info", 1);
  add("debug", 1);
  add("error", 2);
  add("warn", 2);
  add("warning", 2);
  return console;
}

bool ConsoleBuiltins::resolve(const std::string &method, int &fileDescriptor) {
  const ObjectPtr console = namespaceObject();
  const Value methodValue = ObjectOperations::get(console, PropertyKey(method));
  if (!methodValue.isString()) return false;
  fileDescriptor = methodValue.asString() == u"stdout" ? 1 : 2;
  return true;
}

std::string formatConsoleValue(const Value &value) {
  if (value.isUndefined()) return "undefined";
  if (value.isNull()) return "null";
  if (value.isBoolean()) return value.asBoolean() ? "true" : "false";
  if (value.isString()) return utf16ToUtf8(value.asString());
  if (value.isNumber()) {
    const double number = value.asNumber();
    if (std::isnan(number)) return "NaN";
    if (std::isinf(number)) return number < 0 ? "-Infinity" : "Infinity";
    if (number == 0) return "0";
    std::ostringstream out;
    out << std::setprecision(15) << number;
    return out.str();
  }
  if (value.isSymbol()) return "Symbol(" + value.asSymbol()->description + ")";
  if (value.isObject() && value.asObject() &&
      value.asObject()->kind == rma::syntax::ObjectKind::Array) {
    auto arr = std::static_pointer_cast<rma::syntax::ArrayObject>(value.asObject());
    std::string out = "[";
    for (std::size_t i = 0; i < arr->elements.size(); ++i) {
      if (i) out += ", ";
      out += formatConsoleValue(arr->elements[i]);
    }
    out += "]";
    return out;
  }
  return "[object Object]";
}

} // namespace rma::compiler
