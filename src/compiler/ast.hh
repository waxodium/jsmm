#pragma once

#include "engine/value.hh"
#include <string>
#include <vector>

namespace rma::compiler {

struct PrintStatement {
  int fd = 1;
  std::vector<edon::ecma::Value> arguments;
};

struct Program {
  std::vector<PrintStatement> output;
};

} // namespace rma::compiler
