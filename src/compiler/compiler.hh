#pragma once

#include <string>

namespace rma::compiler {

class Compiler final {
public:
  void compileFile(const std::string &inputPath, const std::string &outputPath) const;
};

} // namespace rma::compiler
