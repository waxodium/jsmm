#pragma once

#include "codegen.hh"
#include <cstdint>
#include <vector>

namespace rma::compiler {

class ElfWriter final {
public:
  std::vector<std::uint8_t> write(const NativeImage &image) const;
};

} // namespace rma::compiler
