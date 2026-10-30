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
  lea rdx, [rip + jsmm_arena_end]
  cmp rcx, QWORD PTR [rdx]
  jbe jsmm_alloc_commit
  mov rsi, 67108864
  cmp rdi, rsi
  jbe jsmm_alloc_size
  lea rsi, [rdi + 32]
jsmm_alloc_size:
  push rdi
  xor edi, edi
  mov edx, 3
  mov r10d, 34
  mov r8, -1
  xor r9d, r9d
  mov eax, 9
  syscall
  pop rdi
  cmp rax, -4095
  jae jsmm_alloc_oom
  mov rdx, rax
  lea rcx, [rax + rdi + 15]
  and rcx, -16
  add rdx, rsi
  mov QWORD PTR [rip + jsmm_arena_pointer], rcx
  mov QWORD PTR [rip + jsmm_arena_end], rdx
  ret
jsmm_alloc_commit:
  mov QWORD PTR [rip + jsmm_arena_pointer], rcx
  ret
jsmm_alloc_oom:
  mov eax, 1
  mov edi, 2
  lea rsi, [rip + jsmm_message_oom]
  mov edx, offset jsmm_message_oom_len
  syscall
  mov edi, 1
  mov eax, 60
  syscall
.section .data
.align 8
jsmm_arena_pointer:
  .quad jsmm_arena
jsmm_arena_end:
  .quad jsmm_arena + 67108864
.section .rodata
jsmm_message_oom:
  .ascii "jsmm: out of memory\n"
jsmm_message_oom_end:
.set jsmm_message_oom_len, jsmm_message_oom_end - jsmm_message_oom
.section .bss,"aw",@nobits
.align 16
jsmm_arena:
  .zero 67108864
)ASM";

constexpr const char *moduloText = R"ASM(.section .text
.globl jsmm_mod
jsmm_mod:
  pxor xmm2, xmm2
  ucomisd xmm1, xmm2
  jp jsmm_mod_nan
  je jsmm_mod_nan
  movq rax, xmm0
  movabs rcx, 0x7FFFFFFFFFFFFFFF
  and rax, rcx
  movabs rcx, 0x7FF0000000000000
  cmp rax, rcx
  jae jsmm_mod_nan
  movq rax, xmm1
  movabs rcx, 0x7FFFFFFFFFFFFFFF
  and rax, rcx
  movabs rcx, 0x7FF0000000000000
  cmp rax, rcx
  je jsmm_mod_return
  movsd xmm2, xmm0
  divsd xmm0, xmm1
  cvttsd2si rax, xmm0
  cvtsi2sd xmm0, rax
  mulsd xmm0, xmm1
  subsd xmm2, xmm0
  movsd xmm0, xmm2
  ret
jsmm_mod_return:
  ret
jsmm_mod_nan:
  pxor xmm2, xmm2
  divsd xmm2, xmm2
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
  mov edx, offset jsmm_message_pow_len
  syscall
  mov edi, 1
  mov eax, 60
  syscall
.section .data
jsmm_message_pow:
  .ascii "jsmm: ** requires a non-negative integer exponent\n"
jsmm_message_pow_end:
.set jsmm_message_pow_len, jsmm_message_pow_end - jsmm_message_pow
)ASM";

constexpr const char *int32Text = R"ASM(.section .text
.globl jsmm_to_i32
jsmm_to_i32:
  cvttsd2si rax, xmm0
  movsxd rax, eax
  ret
.globl jsmm_i32_done
jsmm_i32_done:
  movsxd rax, eax
  ret
.globl jsmm_u32_done
jsmm_u32_done:
  mov eax, eax
  ret
)ASM";

constexpr const char *boundsText = R"ASM(.section .text
.globl jsmm_oob
jsmm_oob:
  mov eax, 1
  mov edi, 2
  lea rsi, [rip + jsmm_message_bounds]
  mov edx, offset jsmm_message_bounds_len
  syscall
  mov edi, 1
  mov eax, 60
  syscall
.section .data
jsmm_message_bounds:
  .ascii "jsmm: index out of bounds\n"
jsmm_message_bounds_end:
.set jsmm_message_bounds_len, jsmm_message_bounds_end - jsmm_message_bounds
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

constexpr const char *throwText = R"ASM(.section .text
.globl jsmm_throw
jsmm_throw:
  mov eax, 1
  mov edi, 2
  lea rsi, [rip + jsmm_message_throw]
  mov edx, offset jsmm_message_throw_len
  syscall
  mov edi, 1
  mov eax, 60
  syscall
.section .data
jsmm_message_throw:
  .ascii "jsmm: uncaught exception\n"
jsmm_message_throw_end:
.set jsmm_message_throw_len, jsmm_message_throw_end - jsmm_message_throw
)ASM";

constexpr const char *stringEqualText = R"ASM(.section .text
.globl jsmm_str_eq
jsmm_str_eq:
  mov rax, QWORD PTR [rdi]
  cmp rax, QWORD PTR [rsi]
  jne jsmm_str_eq_false
  lea rdi, [rdi + 8]
  lea rsi, [rsi + 8]
  xor rcx, rcx
jsmm_str_eq_loop:
  cmp rcx, rax
  jae jsmm_str_eq_true
  mov dl, BYTE PTR [rdi + rcx]
  cmp dl, BYTE PTR [rsi + rcx]
  jne jsmm_str_eq_false
  inc rcx
  jmp jsmm_str_eq_loop
jsmm_str_eq_true:
  mov eax, 1
  ret
jsmm_str_eq_false:
  xor eax, eax
  ret
)ASM";

constexpr const char *stringConcatText = R"ASM(.section .text
.globl jsmm_str_cat
jsmm_str_cat:
  push rbx
  push r12
  push r13
  mov rbx, rdi
  mov r12, rsi
  mov r13, QWORD PTR [rbx]
  add r13, QWORD PTR [r12]
  lea rdi, [r13 + 8]
  call jsmm_alloc
  mov QWORD PTR [rax], r13
  lea rdi, [rax + 8]
  lea rsi, [rbx + 8]
  mov rcx, QWORD PTR [rbx]
  rep movsb
  lea rsi, [r12 + 8]
  mov rcx, QWORD PTR [r12]
  rep movsb
  pop r13
  pop r12
  pop rbx
  ret
)ASM";

constexpr const char *bytesCopyText = R"ASM(.section .text
.globl jsmm_bytes_copy
jsmm_bytes_copy:
  push rbx
  push r12
  mov rbx, rdi
  mov r12, QWORD PTR [rbx]
  lea rdi, [r12 + 8]
  call jsmm_alloc
  mov QWORD PTR [rax], r12
  lea rdi, [rax + 8]
  lea rsi, [rbx + 8]
  mov rcx, r12
  rep movsb
  pop r12
  pop rbx
  ret
)ASM";

constexpr const char *stringCompareText = R"ASM(.section .text
.globl jsmm_str_cmp
jsmm_str_cmp:
  mov r10, QWORD PTR [rdi]
  mov r11, QWORD PTR [rsi]
  mov r8, r10
  cmp r8, r11
  cmova r8, r11
  lea rdi, [rdi + 8]
  lea rsi, [rsi + 8]
  xor ecx, ecx
jsmm_str_cmp_loop:
  cmp rcx, r8
  jae jsmm_str_cmp_suffix
  mov al, BYTE PTR [rdi + rcx]
  cmp al, BYTE PTR [rsi + rcx]
  jb jsmm_str_cmp_less
  ja jsmm_str_cmp_greater
  inc rcx
  jmp jsmm_str_cmp_loop
jsmm_str_cmp_suffix:
  cmp r10, r11
  jb jsmm_str_cmp_less
  ja jsmm_str_cmp_greater
  xor eax, eax
  ret
jsmm_str_cmp_less:
  mov rax, -1
  ret
jsmm_str_cmp_greater:
  mov eax, 1
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
  push rbx
  push r12
  push r13
  sub rsp, 128
  mov rbx, rsp
  movq rax, xmm0
  mov r8, rax
  shr r8, 63
  movabs rcx, 0x7FFFFFFFFFFFFFFF
  and rax, rcx
  movabs rcx, 0x7FF0000000000000
  cmp rax, rcx
  je jsmm_number_inf
  ucomisd xmm0, xmm0
  jp jsmm_number_nan
  test rax, rax
  jz jsmm_number_zero
  xor r9d, r9d
  test r8, r8
  jz jsmm_number_positive
  mov byte ptr [rbx], 45
  inc r9
  movabs rax, 0x8000000000000000
  movq xmm1, rax
  xorpd xmm0, xmm1
jsmm_number_positive:
  movsd xmm1, QWORD PTR [rip + jsmm_number_epsilon]
  ucomisd xmm0, xmm1
  jb jsmm_number_scientific
  movsd xmm1, QWORD PTR [rip + jsmm_number_large_limit]
  ucomisd xmm0, xmm1
  jae jsmm_number_scientific
  movsd xmm1, QWORD PTR [rip + jsmm_number_plain_limit]
  ucomisd xmm0, xmm1
  jae jsmm_number_large
  cvttsd2si r12, xmm0
  cvtsi2sd xmm1, r12
  movsd xmm2, xmm0
  subsd xmm2, xmm1
  mulsd xmm2, QWORD PTR [rip + jsmm_number_million]
  addsd xmm2, QWORD PTR [rip + jsmm_number_half]
  cvttsd2si r13, xmm2
  cmp r13, 1000000
  jb jsmm_number_normal_ready
  inc r12
  xor r13d, r13d
jsmm_number_normal_ready:
  mov rax, r12
  call jsmm_number_int
  mov rdx, r13
  mov ecx, 6
  call jsmm_number_frac
  test r10, r10
  jz jsmm_number_emit
  mov byte ptr [rbx + r9], 46
  inc r9
  xor ecx, ecx
jsmm_number_normal_copy:
  mov al, byte ptr [rbx + rcx + 48]
  mov byte ptr [rbx + r9], al
  inc r9
  inc rcx
  cmp rcx, r10
  jb jsmm_number_normal_copy
  jmp jsmm_number_emit
jsmm_number_large:
  xor ecx, ecx
  movsd xmm1, QWORD PTR [rip + jsmm_number_plain_limit]
jsmm_number_large_loop:
  ucomisd xmm0, xmm1
  jb jsmm_number_large_ready
  divsd xmm0, QWORD PTR [rip + jsmm_number_ten]
  inc ecx
  jmp jsmm_number_large_loop
jsmm_number_large_ready:
  cvttsd2si r12, xmm0
  mov r13, rcx
  mov rax, r12
  call jsmm_number_int
  mov rcx, r13
  test rcx, rcx
  jz jsmm_number_emit
jsmm_number_large_zero:
  mov byte ptr [rbx + r9], 48
  inc r9
  dec rcx
  jnz jsmm_number_large_zero
  jmp jsmm_number_emit
jsmm_number_scientific:
  xor r12d, r12d
  movsd xmm1, QWORD PTR [rip + jsmm_number_ten]
jsmm_number_scientific_big:
  ucomisd xmm0, xmm1
  jb jsmm_number_scientific_small_start
  divsd xmm0, QWORD PTR [rip + jsmm_number_ten]
  inc r12
  jmp jsmm_number_scientific_big
jsmm_number_scientific_small_start:
  movsd xmm1, QWORD PTR [rip + jsmm_number_one]
jsmm_number_scientific_small:
  ucomisd xmm0, xmm1
  jae jsmm_number_scientific_ready
  mulsd xmm0, QWORD PTR [rip + jsmm_number_ten]
  dec r12
  jmp jsmm_number_scientific_small
jsmm_number_scientific_ready:
  cvttsd2si r8, xmm0
  cvtsi2sd xmm1, r8
  movsd xmm2, xmm0
  subsd xmm2, xmm1
  mulsd xmm2, QWORD PTR [rip + jsmm_number_billion]
  addsd xmm2, QWORD PTR [rip + jsmm_number_half]
  cvttsd2si r13, xmm2
  cmp r13, 1000000000
  jb jsmm_number_scientific_ready2
  inc r8
  xor r13d, r13d
  cmp r8, 10
  jb jsmm_number_scientific_ready2
  mov r8d, 1
  inc r12
jsmm_number_scientific_ready2:
  mov rax, r8
  add rax, 48
  mov byte ptr [rbx + r9], al
  inc r9
  mov rdx, r13
  mov ecx, 9
  call jsmm_number_frac
  test r10, r10
  jz jsmm_number_scientific_exp
  mov byte ptr [rbx + r9], 46
  inc r9
  xor ecx, ecx
jsmm_number_scientific_copy:
  mov al, byte ptr [rbx + rcx + 48]
  mov byte ptr [rbx + r9], al
  inc r9
  inc rcx
  cmp rcx, r10
  jb jsmm_number_scientific_copy
jsmm_number_scientific_exp:
  mov byte ptr [rbx + r9], 101
  inc r9
  test r12, r12
  jns jsmm_number_scientific_plus
  mov byte ptr [rbx + r9], 45
  inc r9
  neg r12
  jmp jsmm_number_scientific_digits
jsmm_number_scientific_plus:
  mov byte ptr [rbx + r9], 43
  inc r9
jsmm_number_scientific_digits:
  mov rax, r12
  call jsmm_number_int
  jmp jsmm_number_emit
jsmm_number_zero:
  mov byte ptr [rbx], 48
  mov r9, 1
  jmp jsmm_number_emit
jsmm_number_nan:
  lea rdi, [rip + jsmm_literal_nan]
  jmp jsmm_number_special
jsmm_number_inf:
  test r8, r8
  lea rdi, [rip + jsmm_literal_infinity]
  jz jsmm_number_special
  lea rdi, [rip + jsmm_literal_neg_infinity]
jsmm_number_special:
  mov rdx, QWORD PTR [rdi]
  lea rsi, [rdi + 8]
  mov eax, 1
  mov edi, 1
  syscall
  jmp jsmm_number_done
jsmm_number_emit:
  mov rsi, rbx
  mov rdx, r9
  mov eax, 1
  mov edi, 1
  syscall
jsmm_number_done:
  add rsp, 128
  pop r13
  pop r12
  pop rbx
  ret
jsmm_number_int:
  lea rdi, [rbx + 80]
  test rax, rax
  jnz jsmm_number_int_loop
  dec rdi
  mov byte ptr [rdi], 48
  jmp jsmm_number_int_copy
jsmm_number_int_loop:
  xor rdx, rdx
  mov r11, 10
  div r11
  add dl, 48
  dec rdi
  mov byte ptr [rdi], dl
  test rax, rax
  jnz jsmm_number_int_loop
jsmm_number_int_copy:
  lea rsi, [rbx + 80]
jsmm_number_int_copy_loop:
  cmp rdi, rsi
  jae jsmm_number_int_done
  mov al, byte ptr [rdi]
  mov byte ptr [rbx + r9], al
  inc rdi
  inc r9
  jmp jsmm_number_int_copy_loop
jsmm_number_int_done:
  ret
jsmm_number_frac:
  lea rdi, [rbx + rcx + 47]
  mov r10, rcx
  mov rax, rdx
jsmm_number_frac_loop:
  xor rdx, rdx
  mov r11, 10
  div r11
  add dl, 48
  mov byte ptr [rdi], dl
  dec rdi
  dec rcx
  jnz jsmm_number_frac_loop
jsmm_number_frac_trim:
  test r10, r10
  jz jsmm_number_frac_done
  lea rdi, [rbx + r10 + 47]
  cmp byte ptr [rdi], 48
  jne jsmm_number_frac_done
  dec r10
  jmp jsmm_number_frac_trim
jsmm_number_frac_done:
  ret
.section .rodata
.align 8
jsmm_number_epsilon:
  .quad 0x3eb0c6f7a0b5ed8d
jsmm_number_plain_limit:
  .quad 0x43abc16d674ec800
jsmm_number_large_limit:
  .quad 0x444b1ae4d6e2ef50
jsmm_number_million:
  .quad 0x412e848000000000
jsmm_number_billion:
  .quad 0x41cdcd6500000000
jsmm_number_half:
  .quad 0x3fe0000000000000
jsmm_number_ten:
  .quad 0x4024000000000000
jsmm_number_one:
  .quad 0x3ff0000000000000
)ASM";

} // namespace

void registerPlatformHelpers(HelperRegistry &registry) {
  registry.declare(std::string(startSymbol), startText);
  registry.declare(std::string(entryFrameSymbol), entryFrameText);
  registry.declare(std::string(allocSymbol), allocText);
  registry.declare(std::string(moduloSymbol), moduloText);
  registry.declare(std::string(powerSymbol), powerText);
  registry.declare("int32", int32Text);
  registry.declare(std::string(boundsSymbol), boundsText);
  registry.declare("string_writer", stringWriterText);
  registry.declare("string_equal", stringEqualText);
  registry.declare("string_concat", stringConcatText);
  registry.declare("string_compare", stringCompareText);
  registry.declare("bytes_copy", bytesCopyText);
  registry.declare("throw", throwText);
  registry.declare("literals", literalText);
  registry.declare("literal_writers", literalWriterText);
  registry.declare("number_writer", numberWriterText);
}

} // namespace jsmm::emit
