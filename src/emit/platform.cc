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

constexpr const char *entryFrameText = R"ASM(.section .bss
.align 8
rma_entry_frame:
  .zero 8
)ASM";

constexpr const char *stringWriterText = R"ASM(.section .text
.globl rma_cat_str
rma_cat_str:
  mov rdx, QWORD PTR [rdi]
  lea rsi, [rdi + 8]
  mov eax, 1
  mov edi, 1
  syscall
  ret
)ASM";

constexpr const char *literalText = R"ASM(.section .rodata
rma_literal_true:
  .quad 4
  .ascii "true"
rma_literal_false:
  .quad 5
  .ascii "false"
rma_literal_null:
  .quad 4
  .ascii "null"
rma_literal_undefined:
  .quad 9
  .ascii "undefined"
rma_literal_lbracket:
  .quad 1
  .ascii "["
rma_literal_separator:
  .quad 2
  .ascii ", "
rma_literal_rbracket:
  .quad 1
  .ascii "]"
rma_literal_nan:
  .quad 3
  .ascii "NaN"
rma_literal_infinity:
  .quad 8
  .ascii "Infinity"
rma_literal_neg_infinity:
  .quad 9
  .ascii "-Infinity"
rma_literal_newline:
  .quad 1
  .ascii "\n"
rma_literal_empty_string:
  .quad 0
)ASM";

constexpr const char *literalWriterText = R"ASM(.section .text
.globl rma_write_bool
rma_write_bool:
  test rdi, rdi
  lea rdi, [rip + rma_literal_false]
  jz rma_write_bool_emit
  lea rdi, [rip + rma_literal_true]
rma_write_bool_emit:
  jmp rma_cat_str
.globl rma_write_null
rma_write_null:
  lea rdi, [rip + rma_literal_null]
  jmp rma_cat_str
.globl rma_write_undefined
rma_write_undefined:
  lea rdi, [rip + rma_literal_undefined]
  jmp rma_cat_str
.globl rma_write_lbracket
rma_write_lbracket:
  lea rdi, [rip + rma_literal_lbracket]
  jmp rma_cat_str
.globl rma_write_separator
rma_write_separator:
  lea rdi, [rip + rma_literal_separator]
  jmp rma_cat_str
.globl rma_write_rbracket
rma_write_rbracket:
  lea rdi, [rip + rma_literal_rbracket]
  jmp rma_cat_str
)ASM";

constexpr const char *numberWriterText = R"ASM(.section .text
.globl rma_write_number
rma_write_number:
  movq rax, xmm0
  mov r8, rax
  shr r8, 63
  movabs rcx, 0x7FFFFFFFFFFFFFFF
  and rax, rcx
  movabs rcx, 0x7FF0000000000000
  cmp rax, rcx
  jne rma_number_finite
  test r8, r8
  lea rdi, [rip + rma_literal_infinity]
  jz rma_number_special
  lea rdi, [rip + rma_literal_neg_infinity]
  jmp rma_number_special
rma_number_finite:
  ucomisd xmm0, xmm0
  jnp rma_number_convert
  lea rdi, [rip + rma_literal_nan]
rma_number_special:
  mov rdx, QWORD PTR [rdi]
  lea rsi, [rdi + 8]
  mov rax, 1
  mov edi, 1
  syscall
  ret
rma_number_convert:
  push rbx
  sub rsp, 64
  mov rbx, rsp
  xor r9, r9
  pxor xmm1, xmm1
  ucomisd xmm0, xmm1
  jae rma_number_positive
  mov byte ptr [rbx], 45
  mov r9, 1
  movabs rax, 0x8000000000000000
  movq xmm1, rax
  xorpd xmm0, xmm1
rma_number_positive:
  mov rcx, 1000000
  cvtsi2sd xmm2, rcx
  mulsd xmm0, xmm2
  cvttsd2si rax, xmm0
  xor rdx, rdx
  mov rcx, 1000000
  div rcx
  mov r10, rax
  mov r11, rdx
  test r10, r10
  jnz rma_number_integer
  mov byte ptr [rbx + r9], 48
  inc r9
  jmp rma_number_fraction
rma_number_integer:
  mov r8, 32
rma_number_integer_loop:
  xor rdx, rdx
  mov rcx, 10
  div rcx
  add rdx, 48
  mov byte ptr [rbx + r8], dl
  dec r8
  test rax, rax
  jnz rma_number_integer_loop
  inc r8
rma_number_integer_copy:
  mov al, byte ptr [rbx + r8]
  mov byte ptr [rbx + r9], al
  inc r9
  inc r8
  cmp r8, 32
  jbe rma_number_integer_copy
rma_number_fraction:
  test r11, r11
  jz rma_number_emit
  mov byte ptr [rbx + r9], 46
  inc r9
  mov r8, 5
rma_number_fraction_digit:
  mov rax, r11
  xor rdx, rdx
  mov rcx, 10
  div rcx
  mov r11, rax
  add rdx, 48
  mov byte ptr [rbx + r8 + 36], dl
  dec r8
  jns rma_number_fraction_digit
  mov r8, 36
  mov rcx, 42
rma_number_fraction_copy:
  mov al, byte ptr [rbx + r8]
  mov byte ptr [rbx + r9], al
  inc r9
  inc r8
  cmp r8, rcx
  jb rma_number_fraction_copy
rma_number_trim:
  cmp r9, 2
  jbe rma_number_emit
  cmp byte ptr [rbx + r9 - 1], 48
  jne rma_number_emit
  cmp byte ptr [rbx + r9 - 2], 46
  je rma_number_emit
  dec r9
  jmp rma_number_trim
rma_number_emit:
  mov rsi, rbx
  mov rdx, r9
  mov rax, 1
  mov edi, 1
  syscall
  add rsp, 64
  pop rbx
  ret
)ASM";

} // namespace

void registerPlatformHelpers(HelperRegistry &registry) {
  registry.declare(std::string(startSymbol), startText);
  registry.declare(std::string(entryFrameSymbol), entryFrameText);
  registry.declare(std::string(allocSymbol), allocText);
  registry.declare(std::string(moduloSymbol), moduloText);
  registry.declare(std::string(powerSymbol), powerText);
  registry.declare(std::string(boundsSymbol), boundsText);
  registry.declare("string_writer", stringWriterText);
  registry.declare("literals", literalText);
  registry.declare("literal_writers", literalWriterText);
  registry.declare("number_writer", numberWriterText);
}

} // namespace rma::emit
