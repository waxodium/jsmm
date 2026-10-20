#include "emit/platform.hh"

namespace rma::emit {

namespace {

constexpr const char *startText = R"ASM(.section .text
.globl _start
_start:
  xor rbp, rbp
  and rsp, -16
  call rma_entry
  xor edi, edi
  mov eax, 60
  syscall
)ASM";

constexpr const char *allocText = R"ASM(.section .text
.globl rma_alloc
rma_alloc:
  mov rax, QWORD PTR [rip + rma_arena_pointer]
  lea rcx, [rax + rdi + 15]
  and rcx, -16
  mov QWORD PTR [rip + rma_arena_pointer], rcx
  ret
.section .data
.align 8
rma_arena_pointer:
  .quad rma_arena
.section .bss
.align 16
rma_arena:
  .zero 67108864
)ASM";

constexpr const char *moduloText = R"ASM(.section .text
.globl rma_mod
rma_mod:
  movsd xmm2, xmm0
  divsd xmm0, xmm1
  cvttsd2si rax, xmm0
  cvtsi2sd xmm0, rax
  mulsd xmm0, xmm1
  subsd xmm2, xmm0
  movsd xmm0, xmm2
  ret
)ASM";

constexpr const char *powerText = R"ASM(.section .text
.globl rma_pow
rma_pow:
  cvttsd2si rax, xmm1
  cvtsi2sd xmm2, rax
  ucomisd xmm2, xmm1
  jne rma_pow_unsupported
  jp rma_pow_unsupported
  test rax, rax
  js rma_pow_unsupported
  movabs rcx, 0x3FF0000000000000
  movq xmm3, rcx
rma_pow_loop:
  test rax, rax
  je rma_pow_done
  mulsd xmm3, xmm0
  dec rax
  jmp rma_pow_loop
rma_pow_done:
  movsd xmm0, xmm3
  ret
rma_pow_unsupported:
  mov eax, 1
  mov edi, 2
  lea rsi, [rip + rma_message_pow]
  mov edx, rma_message_pow_end - rma_message_pow
  syscall
  mov edi, 1
  mov eax, 60
  syscall
.section .data
rma_message_pow:
  .ascii "rma: ** requires a non-negative integer exponent\n"
rma_message_pow_end:
)ASM";

constexpr const char *boundsText = R"ASM(.section .text
.globl rma_oob
rma_oob:
  mov eax, 1
  mov edi, 2
  lea rsi, [rip + rma_message_bounds]
  mov edx, rma_message_bounds_end - rma_message_bounds
  syscall
  mov edi, 1
  mov eax, 60
  syscall
.section .data
rma_message_bounds:
  .ascii "rma: array index out of bounds\n"
rma_message_bounds_end:
)ASM";

}

void registerPlatformHelpers(HelperRegistry &registry) {
  registry.declare(std::string(startSymbol), startText);
  registry.declare(std::string(allocSymbol), allocText);
  registry.declare(std::string(moduloSymbol), moduloText);
  registry.declare(std::string(powerSymbol), powerText);
  registry.declare(std::string(boundsSymbol), boundsText);
}

}
