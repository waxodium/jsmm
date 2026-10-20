#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rma::emit::x64 {

enum class Reg { Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi, R8, R9, R10, R11 };

enum class Xmm { X0, X1, X2, X3 };

enum class Cond { E, NE, Z, NZ, L, LE, G, GE, B, BE, A, AE, P, NP };

class Assembly final {
public:
  void directive(std::string_view text);
  void globl(std::string_view name);
  void section(std::string_view name);
  void label(std::string_view name);

  void movRegImm(Reg destination, std::uint64_t immediate);
  void movRegReg(Reg destination, Reg source);
  void movOffsetReg(Reg base, long long displacement, Reg source);
  void movRegOffset(Reg destination, Reg base, long long displacement);
  void movIndexReg(Reg base, Reg index, long long displacement, Reg source);
  void movRegIndex(Reg destination, Reg base, Reg index, long long displacement);
  void leaLabel(Reg destination, std::string_view label);
  void push(Reg reg);
  void pop(Reg reg);

  void addRegImm(Reg reg, long long immediate);
  void subRegImm(Reg reg, long long immediate);
  void addRegReg(Reg destination, Reg source);
  void subRegReg(Reg destination, Reg source);
  void andRegReg(Reg destination, Reg source);
  void orRegReg(Reg destination, Reg source);
  void xorRegReg(Reg destination, Reg source);
  void xorRegImm(Reg reg, long long immediate);
  void notReg(Reg reg);
  void imulRegReg(Reg destination, Reg source);
  void movzxByte(Reg destination, Reg source);
  void cmpRegReg(Reg left, Reg right);
  void cmpRegImm(Reg reg, long long immediate);
  void testRegReg(Reg left, Reg right);
  void shlImm(Reg reg, unsigned amount);
  void shrImm(Reg reg, unsigned amount);
  void shlCl(Reg reg);
  void shrCl(Reg reg);
  void sarCl(Reg reg);
  void btcRegImm(Reg reg, unsigned bit);

  void setCond(Cond condition, Reg destination);
  void jmp(std::string_view label);
  void jcc(Cond condition, std::string_view label);
  void call(std::string_view label);
  void callIndirect(Reg reg);
  void ret();
  void syscall();

  void movsdXmmXmm(Xmm destination, Xmm source);
  void movsdOffsetXmm(Reg base, long long displacement, Xmm source);
  void movsdXmmOffset(Xmm destination, Reg base, long long displacement);
  void movsdLabel(Xmm destination, std::string_view label);
  void addsd(Xmm destination, Xmm source);
  void subsd(Xmm destination, Xmm source);
  void mulsd(Xmm destination, Xmm source);
  void divsd(Xmm destination, Xmm source);
  void ucomisd(Xmm left, Xmm right);
  void cvttsd2si(Reg destination, Xmm source);
  void cvtsi2sd(Xmm destination, Reg source);
  void movqXmmReg(Xmm destination, Reg source);
  void movqRegXmm(Reg destination, Xmm source);
  void pxor(Xmm destination, Xmm source);

  void quad(std::uint64_t value);
  void bytes(const std::vector<std::uint8_t> &data);

  [[nodiscard]] const std::string &text() const;
  void append(const Assembly &other);
  void append(std::string text);

private:
  static const char *name(Reg reg);
  static const char *name(Xmm reg);
  static const char *name(Cond condition);
  static std::string offset(Reg base, long long displacement);

  std::string text_;
};

}
