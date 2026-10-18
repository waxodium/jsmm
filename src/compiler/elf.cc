#include "elf.hh"

#include <stdexcept>

namespace rma::compiler {
namespace {
void set16(std::vector<std::uint8_t> &out, std::size_t at, std::uint16_t v) {
  out.at(at) = static_cast<std::uint8_t>(v);
  out.at(at + 1) = static_cast<std::uint8_t>(v >> 8);
}
void set32(std::vector<std::uint8_t> &out, std::size_t at, std::uint32_t v) {
  for (unsigned i = 0; i < 4; ++i) out.at(at + i) = static_cast<std::uint8_t>(v >> (i * 8));
}
void set64(std::vector<std::uint8_t> &out, std::size_t at, std::uint64_t v) {
  for (unsigned i = 0; i < 8; ++i) out.at(at + i) = static_cast<std::uint8_t>(v >> (i * 8));
}
} // namespace

std::vector<std::uint8_t> ElfWriter::write(const NativeImage &image) const {
  constexpr std::uint64_t base = 0x400000;
  constexpr std::size_t eh = 64;
  constexpr std::size_t ph = 56;
  constexpr std::size_t codeOffset = eh + ph;
  if (image.code.size() + image.data.size() > 0xffffffffU)
    throw std::runtime_error("generated ELF exceeds supported size");
  const std::uint64_t size = codeOffset + image.code.size() + image.data.size();
  std::vector<std::uint8_t> out(codeOffset, 0);
  out[0] = 0x7f;
  out[1] = 'E';
  out[2] = 'L';
  out[3] = 'F';
  out[4] = 2;
  out[5] = 1;
  out[6] = 1;
  set16(out, 16, 2);
  set16(out, 18, 62);
  set32(out, 20, 1);
  set64(out, 24, base + codeOffset);
  set64(out, 32, eh);
  set16(out, 52, eh);
  set16(out, 54, ph);
  set16(out, 56, 1);
  set32(out, eh, 1);
  set32(out, eh + 4, 5);
  set64(out, eh + 8, 0);
  set64(out, eh + 16, base);
  set64(out, eh + 24, base);
  set64(out, eh + 32, size);
  set64(out, eh + 40, size);
  set64(out, eh + 48, 0x1000);
  out.insert(out.end(), image.code.begin(), image.code.end());
  out.insert(out.end(), image.data.begin(), image.data.end());
  return out;
}

} // namespace rma::compiler
