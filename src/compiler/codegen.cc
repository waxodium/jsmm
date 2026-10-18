#include "codegen.hh"

#include "console.hh"
#include <limits>
#include <stdexcept>

namespace rma::compiler {
namespace {
void append32(std::vector<std::uint8_t> &out, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
void patch32(std::vector<std::uint8_t> &out, std::size_t offset, std::int32_t value) {
  const auto bits = static_cast<std::uint32_t>(value);
  for (unsigned i = 0; i < 4; ++i) out.at(offset + i) = static_cast<std::uint8_t>(bits >> (i * 8));
}
struct Relocation {
  std::size_t displacement;
  std::size_t dataOffset;
};
} // namespace

NativeImage CodeGenerator::generate(const Program &program) const {
  NativeImage image;
  std::vector<Relocation> relocations;
  for (const PrintStatement &statement : program.output) {
    std::string line;
    for (std::size_t i = 0; i < statement.arguments.size(); ++i) {
      if (i) line += ' ';
      line += formatConsoleValue(statement.arguments[i]);
    }
    line += '\n';
    const std::size_t dataOffset = image.data.size();
    image.data.insert(image.data.end(), line.begin(), line.end());

    // lea rsi, [rip + disp32]
    image.code.insert(image.code.end(), {0x48, 0x8d, 0x35});
    const std::size_t displacement = image.code.size();
    append32(image.code, 0);
    // mov edx, byte count
    image.code.push_back(0xba);
    if (line.size() > std::numeric_limits<std::uint32_t>::max())
      throw std::runtime_error("console output is too large");
    append32(image.code, static_cast<std::uint32_t>(line.size()));
    // mov eax, 1; mov edi, fd; syscall
    image.code.insert(image.code.end(), {0xb8, 0x01, 0x00, 0x00, 0x00, 0xbf});
    append32(image.code, static_cast<std::uint32_t>(statement.fd));
    image.code.insert(image.code.end(), {0x0f, 0x05});
    relocations.push_back({displacement, dataOffset});
  }
  // mov eax, 60; xor edi, edi; syscall
  image.code.insert(image.code.end(), {0xb8, 0x3c, 0x00, 0x00, 0x00, 0x31, 0xff, 0x0f, 0x05});

  constexpr std::size_t elfHeaderSize = 64;
  constexpr std::size_t programHeaderSize = 56;
  constexpr std::size_t codeOffset = elfHeaderSize + programHeaderSize;
  const std::size_t dataOffset = codeOffset + image.code.size();
  for (const Relocation &relocation : relocations) {
    const std::int64_t target = static_cast<std::int64_t>(dataOffset + relocation.dataOffset);
    const std::int64_t nextInstruction =
        static_cast<std::int64_t>(codeOffset + relocation.displacement + 4);
    const std::int64_t delta = target - nextInstruction;
    if (delta < INT32_MIN || delta > INT32_MAX)
      throw std::runtime_error("code relocation out of range");
    patch32(image.code, relocation.displacement, static_cast<std::int32_t>(delta));
  }
  return image;
}

} // namespace rma::compiler
