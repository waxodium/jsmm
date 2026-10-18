#pragma once

#include "engine/object.hh"
#include <string>

namespace rma::compiler {

class ConsoleBuiltins final {
public:
  static bool resolve(const std::string &method, int &fileDescriptor);
  static edon::ecma::ObjectPtr namespaceObject();
};

std::string formatConsoleValue(const edon::ecma::Value &value);

} // namespace rma::compiler
