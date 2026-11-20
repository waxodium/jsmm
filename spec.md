# JSmm Stabilization Specification

## §3.10 auto Declaration

### Current Behavior
- `auto x;` compiles and zero-initializes to 0.
- The implementation allows `auto` without an initializer, zero-initializing it for its inferred type.

### Spec Update
> An auto declaration may omit an initializer; it is zero-initialized for its inferred type.

This matches the implementation. No change needed.

## §5.5.1 Float Semantics

### Current Behavior
- All floating-point arithmetic is performed in 64-bit IEEE 754.
- After each operation, the result is rounded to the declared float bit-layout width, simulating hardware rounding.
- Storage on write also rounds to the declared width.

### Spec Text
> All floating-point arithmetic is performed in 64-bit IEEE 754. After each operation, the result is rounded to the declared float bit-layout width, simulating hardware rounding. Storage on write also rounds to the declared width.

This is honest. It's not hardware-native float arithmetic, but it produces correctly-rounded results at the declared width. For a stabilized language, honesty about the implementation is more important than pretending it's something it's not.

> **Note**: Do not attempt true width-native float arithmetic (LLVM has no arbitrary-width float type). That would require software emulation of every float operation — a massive undertaking outside the freeze.

## §5.5.2 ABI (Application Binary Interface)

### Parameter Passing Order
- Left-to-right

### Return Value
- In 64-bit cell: i64 for ints/pointers, f64 for floats

### Stack Alignment
- 16-byte (from platform.cc allocator)

### Variadic
- `va_count` (i64) + `va_array` (i64*) after fixed params

### No tail-call guarantee
### No callee-saved register guarantees (LLVM handles it)

## §5.5.3 Memory Model

### Allocator
- Bump-pointer arena allocator

### Alignment
- 16-byte alignment

### Initial Arena Size
- 64KiB, grows via mmap (Linux x86-64 only)

### free() is a no-op
- No garbage collection

### Memory Reclamation
- All memory reclaimed at process exit

### Stack Overflow / OOM
- Terminates via `bounds()` runtime helper

### Stack-allocated locals
- Use native stack; lifetime ends at block/function scope

> **Note**: This is Linux/x86-64 specific. State explicitly that there is no portable memory model yet.

## Bitwise Operations

### Current Behavior
- `&, |, ^, ~`: LLVM integer And/Or/Xor/Not at the declared width
- `<<, >>`: LLVM Shl/LShr/AShr at the declared width, with count masked to width-1
- No f64 conversion needed → no 2^53 precision loss (after Decision 1 is implemented)

### Limitation (before Decision 1)
- Bitwise operations on values >2^53 lossy when stored as f64 cells
- This is a known limitation until the cell representation refactor is complete

## auto without Initializer

### Current Behavior
- `auto x;` compiles and zero-initializes to 0.
- Spec §3.10 originally said initializer is mandatory, but implementation allows it without.

### Spec Text
> An auto declaration may omit an initializer; it is zero-initialized for its inferred type.

This matches the implementation. Don't break it.
