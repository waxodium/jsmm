#pragma once

#include "ast.hh"
#include <cstdint>
#include <vector>

namespace rma::compiler {

struct NativeImage {
  std::vector<std::uint8_t> code;
  std::vector<std::uint8_t> data;
};

class CodeGenerator final {
public:
  NativeImage generate(const Program &program) const;
};

} // namespace rma::compiler
