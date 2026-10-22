#include "emit/x64/assembly.hh"

#include <iomanip>
#include <sstream>

namespace jsmm::emit::x64 {

const char *Assembly::name(Reg reg) {
  switch (reg) {
    case Reg::Rax: return "rax";
    case Reg::Rcx: return "rcx";
    case Reg::Rdx: return "rdx";
    case Reg::Rbx: return "rbx";
    case Reg::Rsp: return "rsp";
    case Reg::Rbp: return "rbp";
    case Reg::Rsi: return "rsi";
    case Reg::Rdi: return "rdi";
    case Reg::R8: return "r8";
    case Reg::R9: return "r9";
    case Reg::R10: return "r10";
    case Reg::R11: return "r11";
  }
  return "rax";
}

const char *Assembly::name(Xmm reg) {
  switch (reg) {
    case Xmm::X0: return "xmm0";
    case Xmm::X1: return "xmm1";
    case Xmm::X2: return "xmm2";
    case Xmm::X3: return "xmm3";
  }
  return "xmm0";
}

const char *Assembly::name(Cond condition) {
  switch (condition) {
    case Cond::E: return "e";
    case Cond::NE: return "ne";
    case Cond::Z: return "z";
    case Cond::NZ: return "nz";
    case Cond::L: return "l";
    case Cond::LE: return "le";
    case Cond::G: return "g";
    case Cond::GE: return "ge";
    case Cond::B: return "b";
    case Cond::BE: return "be";
    case Cond::A: return "a";
    case Cond::AE: return "ae";
    case Cond::P: return "p";
    case Cond::NP: return "np";
  }
  return "e";
}

std::string Assembly::offset(Reg base, long long displacement) {
  std::ostringstream out;
  out << "QWORD PTR [" << name(base);
  if (displacement > 0)
    out << " + " << displacement;
  else if (displacement < 0)
    out << " - " << -displacement;
  out << "]";
  return out.str();
}

void Assembly::directive(std::string_view text) { text_ += std::string(text) + "\n"; }

void Assembly::globl(std::string_view name) { text_ += "  .globl " + std::string(name) + "\n"; }

void Assembly::section(std::string_view name) {
  text_ += ".section ." + std::string(name) + "\n";
}

void Assembly::label(std::string_view name) { text_ += std::string(name) + ":\n"; }

void Assembly::movRegImm(Reg destination, std::uint64_t immediate) {
  std::ostringstream out;
  out << "  movabs " << name(destination) << ", 0x" << std::hex << immediate << "\n";
  text_ += out.str();
}

void Assembly::movRegReg(Reg destination, Reg source) {
  text_ += "  mov " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::movOffsetReg(Reg base, long long displacement, Reg source) {
  text_ += "  mov " + offset(base, displacement) + ", " + name(source) + "\n";
}

void Assembly::movRegOffset(Reg destination, Reg base, long long displacement) {
  text_ += "  mov " + std::string(name(destination)) + ", " + offset(base, displacement) + "\n";
}

void Assembly::movIndexReg(Reg base, Reg index, long long displacement, Reg source) {
  std::ostringstream out;
  out << "  mov QWORD PTR [" << name(base) << " + " << name(index);
  if (displacement > 0)
    out << " + " << displacement;
  else if (displacement < 0)
    out << " - " << -displacement;
  out << "], " << name(source) << "\n";
  text_ += out.str();
}

void Assembly::movRegIndex(Reg destination, Reg base, Reg index, long long displacement) {
  std::ostringstream out;
  out << "  mov " << name(destination) << ", QWORD PTR [" << name(base) << " + " << name(index);
  if (displacement > 0)
    out << " + " << displacement;
  else if (displacement < 0)
    out << " - " << -displacement;
  out << "]\n";
  text_ += out.str();
}

void Assembly::leaLabel(Reg destination, std::string_view label) {
  text_ += "  lea " + std::string(name(destination)) + ", [rip + " + std::string(label) + "]\n";
}

void Assembly::push(Reg reg) { text_ += "  push " + std::string(name(reg)) + "\n"; }

void Assembly::pop(Reg reg) { text_ += "  pop " + std::string(name(reg)) + "\n"; }

void Assembly::addRegImm(Reg reg, long long immediate) {
  text_ += "  add " + std::string(name(reg)) + ", " + std::to_string(immediate) + "\n";
}

void Assembly::subRegImm(Reg reg, long long immediate) {
  text_ += "  sub " + std::string(name(reg)) + ", " + std::to_string(immediate) + "\n";
}

void Assembly::addRegReg(Reg destination, Reg source) {
  text_ += "  add " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::subRegReg(Reg destination, Reg source) {
  text_ += "  sub " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::andRegReg(Reg destination, Reg source) {
  text_ += "  and " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::orRegReg(Reg destination, Reg source) {
  text_ += "  or " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::xorRegReg(Reg destination, Reg source) {
  text_ += "  xor " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::xorRegImm(Reg reg, long long immediate) {
  text_ += "  xor " + std::string(name(reg)) + ", " + std::to_string(immediate) + "\n";
}

void Assembly::notReg(Reg reg) { text_ += "  not " + std::string(name(reg)) + "\n"; }

void Assembly::imulRegReg(Reg destination, Reg source) {
  text_ += "  imul " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::movzxByte(Reg destination, Reg source) {
  const char *low = source == Reg::Rcx ? "cl" : "al";
  text_ += "  movzx " + std::string(name(destination)) + ", " + std::string(low) + "\n";
}

void Assembly::cmpRegReg(Reg left, Reg right) {
  text_ += "  cmp " + std::string(name(left)) + ", " + name(right) + "\n";
}

void Assembly::cmpRegImm(Reg reg, long long immediate) {
  text_ += "  cmp " + std::string(name(reg)) + ", " + std::to_string(immediate) + "\n";
}

void Assembly::testRegReg(Reg left, Reg right) {
  text_ += "  test " + std::string(name(left)) + ", " + name(right) + "\n";
}

void Assembly::shlImm(Reg reg, unsigned amount) {
  text_ += "  shl " + std::string(name(reg)) + ", " + std::to_string(amount) + "\n";
}

void Assembly::shrImm(Reg reg, unsigned amount) {
  text_ += "  shr " + std::string(name(reg)) + ", " + std::to_string(amount) + "\n";
}

void Assembly::shlCl(Reg reg) { text_ += "  shl " + std::string(name(reg)) + ", cl\n"; }

void Assembly::shrCl(Reg reg) { text_ += "  shr " + std::string(name(reg)) + ", cl\n"; }

void Assembly::sarCl(Reg reg) { text_ += "  sar " + std::string(name(reg)) + ", cl\n"; }

void Assembly::btcRegImm(Reg reg, unsigned bit) {
  text_ += "  btc " + std::string(name(reg)) + ", " + std::to_string(bit) + "\n";
}

void Assembly::setCond(Cond condition, Reg destination) {
  const char *low = destination == Reg::Rcx ? "cl" : "al";
  text_ += "  set" + std::string(name(condition)) + " " + std::string(low) + "\n";
}

void Assembly::jmp(std::string_view label) { text_ += "  jmp " + std::string(label) + "\n"; }

void Assembly::jcc(Cond condition, std::string_view label) {
  text_ += "  j" + std::string(name(condition)) + " " + std::string(label) + "\n";
}

void Assembly::call(std::string_view label) { text_ += "  call " + std::string(label) + "\n"; }

void Assembly::callIndirect(Reg reg) { text_ += "  call " + std::string(name(reg)) + "\n"; }

void Assembly::ret() { text_ += "  ret\n"; }

void Assembly::syscall() { text_ += "  syscall\n"; }

void Assembly::movsdXmmXmm(Xmm destination, Xmm source) {
  text_ += "  movsd " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::movsdOffsetXmm(Reg base, long long displacement, Xmm source) {
  std::ostringstream out;
  out << "  movsd " << offset(base, displacement) << ", " << name(source) << "\n";
  text_ += out.str();
}

void Assembly::movsdXmmOffset(Xmm destination, Reg base, long long displacement) {
  std::ostringstream out;
  out << "  movsd " << name(destination) << ", " << offset(base, displacement) << "\n";
  text_ += out.str();
}

void Assembly::movsdLabel(Xmm destination, std::string_view label) {
  text_ += "  movsd " + std::string(name(destination)) + ", QWORD PTR [rip + " +
           std::string(label) + "]\n";
}

void Assembly::addsd(Xmm destination, Xmm source) {
  text_ += "  addsd " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::subsd(Xmm destination, Xmm source) {
  text_ += "  subsd " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::mulsd(Xmm destination, Xmm source) {
  text_ += "  mulsd " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::divsd(Xmm destination, Xmm source) {
  text_ += "  divsd " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::ucomisd(Xmm left, Xmm right) {
  text_ += "  ucomisd " + std::string(name(left)) + ", " + name(right) + "\n";
}

void Assembly::cvttsd2si(Reg destination, Xmm source) {
  text_ += "  cvttsd2si " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::cvtsi2sd(Xmm destination, Reg source) {
  text_ += "  cvtsi2sd " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::movqXmmReg(Xmm destination, Reg source) {
  text_ += "  movq " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::movqRegXmm(Reg destination, Xmm source) {
  text_ += "  movq " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::pxor(Xmm destination, Xmm source) {
  text_ += "  pxor " + std::string(name(destination)) + ", " + name(source) + "\n";
}

void Assembly::quad(std::uint64_t value) {
  std::ostringstream out;
  out << "  .quad 0x" << std::hex << value << "\n";
  text_ += out.str();
}

void Assembly::bytes(const std::vector<std::uint8_t> &data) {
  std::ostringstream out;
  out << "  .byte ";
  for (std::size_t i = 0; i < data.size(); ++i) {
    if (i != 0) out << ", ";
    out << std::dec << static_cast<unsigned>(data[i]);
  }
  out << "\n";
  text_ += out.str();
}

const std::string &Assembly::text() const { return text_; }

void Assembly::append(const Assembly &other) { text_ += other.text_; }

void Assembly::append(std::string text) { text_ += text; }

}
