#pragma once

#include "ast.hh"

#include <iosfwd>

namespace rma::compiler {

class AstPrinter final {
public:
  void print(const Program &program, std::ostream &out, bool showRanges = true) const;
};

} // namespace rma::compiler
