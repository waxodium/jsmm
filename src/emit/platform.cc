#include "emit/platform.hh"

namespace jsmm::emit {

namespace {

constexpr const char *startText = R"ASM(.section .text
.globl _start
_start:
  xor rbp, rbp
  and rsp, -16
  call jsmm_entry
  xor edi, edi
  mov eax, 60
  syscall
)ASM";

constexpr const char *allocText = R"ASM(.section .text
.globl jsmm_alloc
jsmm_alloc:
  mov rax, QWORD PTR [rip + jsmm_arena_pointer]
  lea rcx, [rax + rdi + 15]
  and rcx, -16
  mov QWORD PTR [rip + jsmm_arena_pointer], rcx
  ret
.section .data
.align 8
jsmm_arena_pointer:
  .quad jsmm_arena
.section .bss
.align 16
jsmm_arena:
  .zero 67108864
)ASM";

constexpr const char *moduloText = R"ASM(.section .text
.globl jsmm_mod
jsmm_mod:
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
.globl jsmm_pow
jsmm_pow:
  cvttsd2si rax, xmm1
  cvtsi2sd xmm2, rax
  ucomisd xmm2, xmm1
  jne jsmm_pow_unsupported
  jp jsmm_pow_unsupported
  test rax, rax
  js jsmm_pow_unsupported
  movabs rcx, 0x3FF0000000000000
  movq xmm3, rcx
jsmm_pow_loop:
  test rax, rax
  je jsmm_pow_done
  mulsd xmm3, xmm0
  dec rax
  jmp jsmm_pow_loop
jsmm_pow_done:
  movsd xmm0, xmm3
  ret
jsmm_pow_unsupported:
  mov eax, 1
  mov edi, 2
  lea rsi, [rip + jsmm_message_pow]
  mov edx, jsmm_message_pow_end - jsmm_message_pow
  syscall
  mov edi, 1
  mov eax, 60
  syscall
.section .data
jsmm_message_pow:
  .ascii "jsmm: ** requires a non-negative integer exponent\n"
jsmm_message_pow_end:
)ASM";

constexpr const char *boundsText = R"ASM(.section .text
.globl jsmm_oob
jsmm_oob:
  mov eax, 1
  mov edi, 2
  lea rsi, [rip + jsmm_message_bounds]
  mov edx, jsmm_message_bounds_end - jsmm_message_bounds
  syscall
  mov edi, 1
  mov eax, 60
  syscall
.section .data
jsmm_message_bounds:
  .ascii "jsmm: array index out of bounds\n"
jsmm_message_bounds_end:
)ASM";

constexpr const char *entryFrameText = R"ASM(.section .bss
.align 8
jsmm_entry_frame:
  .zero 8
)ASM";

constexpr const char *stringWriterText = R"ASM(.section .text
.globl jsmm_cat_str
jsmm_cat_str:
  mov rdx, QWORD PTR [rdi]
  lea rsi, [rdi + 8]
  mov eax, 1
  mov edi, 1
  syscall
  ret
)ASM";

constexpr const char *literalText = R"ASM(.section .rodata
jsmm_literal_true:
  .quad 4
  .ascii "true"
jsmm_literal_false:
  .quad 5
  .ascii "false"
jsmm_literal_null:
  .quad 4
  .ascii "null"
jsmm_literal_undefined:
  .quad 9
  .ascii "undefined"
jsmm_literal_lbracket:
  .quad 1
  .ascii "["
jsmm_literal_separator:
  .quad 2
  .ascii ", "
jsmm_literal_rbracket:
  .quad 1
  .ascii "]"
jsmm_literal_nan:
  .quad 3
  .ascii "NaN"
jsmm_literal_infinity:
  .quad 8
  .ascii "Infinity"
jsmm_literal_neg_infinity:
  .quad 9
  .ascii "-Infinity"
jsmm_literal_newline:
  .quad 1
  .ascii "\n"
jsmm_literal_empty_string:
  .quad 0
)ASM";

constexpr const char *literalWriterText = R"ASM(.section .text
.globl jsmm_write_bool
jsmm_write_bool:
  test rdi, rdi
  lea rdi, [rip + jsmm_literal_false]
  jz jsmm_write_bool_emit
  lea rdi, [rip + jsmm_literal_true]
jsmm_write_bool_emit:
  jmp jsmm_cat_str
.globl jsmm_write_null
jsmm_write_null:
  lea rdi, [rip + jsmm_literal_null]
  jmp jsmm_cat_str
.globl jsmm_write_undefined
jsmm_write_undefined:
  lea rdi, [rip + jsmm_literal_undefined]
  jmp jsmm_cat_str
.globl jsmm_write_lbracket
jsmm_write_lbracket:
  lea rdi, [rip + jsmm_literal_lbracket]
  jmp jsmm_cat_str
.globl jsmm_write_separator
jsmm_write_separator:
  lea rdi, [rip + jsmm_literal_separator]
  jmp jsmm_cat_str
.globl jsmm_write_rbracket
jsmm_write_rbracket:
  lea rdi, [rip + jsmm_literal_rbracket]
  jmp jsmm_cat_str
)ASM";

constexpr const char *numberWriterText = R"ASM(.section .text
.globl jsmm_write_number
jsmm_write_number:
  movq rax, xmm0
  mov r8, rax
  shr r8, 63
  movabs rcx, 0x7FFFFFFFFFFFFFFF
  and rax, rcx
  movabs rcx, 0x7FF0000000000000
  cmp rax, rcx
  jne jsmm_number_finite
  test r8, r8
  lea rdi, [rip + jsmm_literal_infinity]
  jz jsmm_number_special
  lea rdi, [rip + jsmm_literal_neg_infinity]
  jmp jsmm_number_special
jsmm_number_finite:
  ucomisd xmm0, xmm0
  jnp jsmm_number_convert
  lea rdi, [rip + jsmm_literal_nan]
jsmm_number_special:
  mov rdx, QWORD PTR [rdi]
  lea rsi, [rdi + 8]
  mov rax, 1
  mov edi, 1
  syscall
  ret
jsmm_number_convert:
  push rbx
  sub rsp, 64
  mov rbx, rsp
  xor r9, r9
  pxor xmm1, xmm1
  ucomisd xmm0, xmm1
  jae jsmm_number_positive
  mov byte ptr [rbx], 45
  mov r9, 1
  movabs rax, 0x8000000000000000
  movq xmm1, rax
  xorpd xmm0, xmm1
jsmm_number_positive:
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
  jnz jsmm_number_integer
  mov byte ptr [rbx + r9], 48
  inc r9
  jmp jsmm_number_fraction
jsmm_number_integer:
  mov r8, 32
jsmm_number_integer_loop:
  xor rdx, rdx
  mov rcx, 10
  div rcx
  add rdx, 48
  mov byte ptr [rbx + r8], dl
  dec r8
  test rax, rax
  jnz jsmm_number_integer_loop
  inc r8
jsmm_number_integer_copy:
  mov al, byte ptr [rbx + r8]
  mov byte ptr [rbx + r9], al
  inc r9
  inc r8
  cmp r8, 32
  jbe jsmm_number_integer_copy
jsmm_number_fraction:
  test r11, r11
  jz jsmm_number_emit
  mov byte ptr [rbx + r9], 46
  inc r9
  mov r8, 5
jsmm_number_fraction_digit:
  mov rax, r11
  xor rdx, rdx
  mov rcx, 10
  div rcx
  mov r11, rax
  add rdx, 48
  mov byte ptr [rbx + r8 + 36], dl
  dec r8
  jns jsmm_number_fraction_digit
  mov r8, 36
  mov rcx, 42
jsmm_number_fraction_copy:
  mov al, byte ptr [rbx + r8]
  mov byte ptr [rbx + r9], al
  inc r9
  inc r8
  cmp r8, rcx
  jb jsmm_number_fraction_copy
jsmm_number_trim:
  cmp r9, 2
  jbe jsmm_number_emit
  cmp byte ptr [rbx + r9 - 1], 48
  jne jsmm_number_emit
  cmp byte ptr [rbx + r9 - 2], 46
  je jsmm_number_emit
  dec r9
  jmp jsmm_number_trim
jsmm_number_emit:
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

} // namespace jsmm::emit
