#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace rma::compiler {
std::vector<std::uint8_t> compileSource(std::string_view source);
class Compiler final {
public:
  void compileFile(const std::string &inputPath, const std::string &outputPath) const;
};
} // namespace rma::compiler
