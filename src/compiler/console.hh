#pragma once

#include "engine/object.hh"
#include <string>

namespace rma::compiler {

class ConsoleBuiltins final {
public:
  static bool resolve(const std::string &method, int &fileDescriptor);
  static rma::syntax::ObjectPtr namespaceObject();
};

std::string formatConsoleValue(const rma::syntax::Value &value);

} // namespace rma::compiler
