# JS-- Language Specification

## 1. Overview

JS-- is a statically typed, natively compiled language with a deliberately small, fixed syntax. It is compiled ahead of time to native code. There is no garbage collection; memory management is implementation-defined and not part of this specification.

The language provides explicit integer type specialization, static type checking with local type inference, block-scoped variables, first-order functions, and a small set of control-flow constructs.

This specification describes the core language: its lexical structure, type system, grammar, and semantics. It intentionally does not describe any standard-library routine or built-in function.

## 2. Lexical Structure

### 2.1 Character set

Source text is read as a sequence of bytes. ASCII letters, digits, and punctuation are recognized directly. UTF-8 encoded text is accepted; identifier characters may include any byte with value 0x80 or greater, which permits multi-byte UTF-8 identifier code points. All matching is case-sensitive.

### 2.2 Whitespace and comments

The following characters separate tokens and are otherwise ignored: space, horizontal tab, vertical tab, form feed, carriage return, and newline.

A line comment begins with // and extends to the end of the line.

A block comment begins with /* and ends with the first */. Block comments may span multiple lines. An unterminated block comment is a lexical error.

If the source begins with #!, the rest of that first line is treated as a shebang and skipped.

### 2.3 Keywords

The following identifiers are reserved keywords. They cannot be used as ordinary names.

| Keyword | Role |
| --- | --- |
| auto | type-inferred variable declaration |
| break | exit from a loop or switch |
| case | switch case label |
| const | read-only variable declaration |
| continue | skip to the next iteration of a loop |
| default | switch default label |
| else | alternative branch of a conditional |
| false | boolean literal |
| for | loop with initialization, condition, update |
| if | conditional branch |
| import | reserved keyword; not implemented in the core language |
| int | integer type (see section 3.2) |
| float | floating-point type (see section 3.3) |
| null | null literal |
| return | return from a function |
| signed | signedness in a sized integer type |
| switch | multi-way branch |
| typeof | type inquiry unary operator |
| true | boolean literal |
| unsigned | unsignedness in a sized integer type |
| void | void type and void unary operator |
| while | condition-controlled loop |
| bytes | bytes type and bytes literal introducer |
| type | type alias declaration |
| string | string type |
| bool | boolean type |
| boolean | boolean type (alias of bool) |

### 2.4 Identifiers

An identifier begins with a letter (a-z, A-Z), an underscore (_), a dollar sign ($), or any byte with value 0x80 or greater. It continues with any of those characters or a digit (0-9). Identifiers are case-sensitive. A sequence that matches a keyword is always tokenized as that keyword, never as an identifier.

### 2.5 Literals

#### 2.5.1 Numeric literals

A numeric literal is one of:

- a decimal integer: one or more digits
- a hexadecimal integer: 0x or 0X followed by one or more hexadecimal digits (0-9, a-f, A-F)
- a binary integer: 0b or 0B followed by one or more binary digits (0, 1)
- an octal integer: 0o or 0O followed by one or more octal digits (0-7)

An optional fractional part (a period followed by digits) and/or an optional exponent (e or E, optionally followed by + or -, then digits) produce a floating-point literal. Examples:

42
0xFF
0b1010
0o77
3.14
1.5e10
.5
42.

There are no integer suffixes such as u or l, and a sign is not part of a literal; a leading - is the unary negation operator. Every numeric literal with a fractional part or exponent denotes a floating-point value of type float (the default 64-bit IEEE 754); every other numeric literal denotes an integer of type int (the default 32-bit signed).

#### 2.5.2 String literals

A string literal is a sequence of characters enclosed in matching double quotes (") or single quotes ('), and is interpreted as UTF-8. A string may not contain an unescaped line terminator.

Escape sequences are:

\n   newline
\t   tab
\r   carriage return
\v   vertical tab
\b   backspace
\f   form feed
\0   null byte
\\   backslash
\'   single quote
\"   double quote
\xHH   hexadecimal byte (two hex digits)
\uHHHH   UTF-16 code unit (four hex digits)
\u{...}   Unicode code point (one to six hex digits)

A string value is self-describing: it carries its own length, which is the number of bytes in the encoded text. The length is an intrinsic property of the string value.

#### 2.5.3 Boolean literals

The boolean literals are true and false.

#### 2.5.4 Null literal

The null literal is null. It denotes the absence of a value.

#### 2.5.5 Bytes literals

A bytes literal is the keyword bytes immediately followed by a string literal or a binary integer literal:

bytes "text"
bytes 0b101010

The result is a bytes value, a distinct type from string.

### 2.6 Operators and delimiters

Delimiters: { } ( ) [ ] . ; , : ... :

The following tokens are operators or compound assignment operators. Whether each position is valid in an expression is defined by the grammar in section 4.

= += -= *= /= %= &= |= ^= <<= >>=
|| && | ^ & == != < > <= >= << >> + - * / %

Unary prefix operators: ! ~ + - & * typeof void

Postfix operators: ( argument-list ) for calls, . for member access, [ expression ] for computed access.

### 2.7 Operator precedence and associativity

| Precedence | Operators | Associativity |
| --- | --- | --- |
| 1 | * / % | left |
| 2 | + - | left |
| 3 | << >> | left |
| 4 | < > <= >= | left |
| 5 | == != | left |
| 6 | & | left |
| 7 | ^ | left |
| 8 | | | left |
| 9 | && | left |
| 10 | || | left |
| 11 (lowest) | = += -= *= /= %= &= |= ^= <<= >>= | right |

Unary prefix operators (!, ~, +, -, &, *, typeof, void) bind tighter than any binary operator. Postfix call, member, and indexed access bind tightest.

Note: there is no binary exponentiation operator. The token ** participates only in pointer declarations (see section 3.5).

## 3. Types

### 3.1 Type categories

The language is statically typed. Every expression has a type determined by declaration, inference, or the rules below. Types are divided into scalar types, composite types, and special types.

### 3.2 Integer types (int specialization)

The keyword int denotes an integer type. Written alone, int means int<32, signed>: a signed 32-bit integer.

Explicit integer specialization is written:

int<width, signed>
int<width, unsigned>

where width is a positive integer. Common specializations:

int<8, signed>    signed 8-bit integer
int<16, signed>   signed 16-bit integer
int<32, signed>   signed 32-bit integer (same as int)
int<32, unsigned> unsigned 32-bit integer
int<64, signed>   signed 64-bit integer
int<64, unsigned> unsigned 64-bit integer

Any positive width is accepted by the type system; storage is rounded up to whole bytes. Practical storage widths are 8, 16, 32, and 64 bits (1, 2, 4, and 8 bytes); other widths are rounded to these standard sizes. Widths above 64 bits are stored in 64-bit cells. The signedness flag controls sign extension on load and the direction of shifts. Shift operators use the rounded storage width of their left operand rather than its declared width, so int<7> shifts at 8 bits (section 5.5).

The bare keyword int is equivalent to int<32, signed>. The two forms may be used interchangeably wherever an integer type is expected.

Examples:

int x = 42;                 // int<32, signed>
int<16, signed> b = 100;    // signed 16-bit integer
int<32, unsigned> u = 42;   // unsigned 32-bit integer
int<8, signed> c = -128;    // signed 8-bit integer

### 3.3 Floating-point types (float specialization)

The keyword float denotes a floating-point type. Written alone, float means float<64>: a 64-bit floating-point number with IEEE 754 double-precision layout.

Explicit floating-point specialization is written in two forms:

**Basic form** — width only:

```
float<bits>
```

where bits is a positive integer rounded up to a whole number of bytes (the same rounding rule as int<width>). The exponent and mantissa are derived from the width using a standard IEEE-like split.

**Advanced form** — full bit layout:

```
float<totalBits, expBits, mantBits, signed>
```

where:
- **totalBits**: Total width of the float format (rounded up to whole bytes for storage)
- **expBits**: Number of bits allocated to the exponent field
- **mantBits**: Number of bits allocated to the mantissa (significand) field
- **signed**: Whether the format includes a sign bit (true/false)

**Layout constraint**:
- `totalBits` must be at least `expBits + mantBits + (signed ? 1 : 0)`
- Any remaining bits are padding: they occupy storage but do not participate in the value
- Padding enables hardware-accurate layouts such as TF32 (`float<32, 8, 10, true>`:
  19 value bits stored in a 32-bit aligned slot)

**Storage width calculation** (same as int<width>):
- Round totalBits up to whole bytes (1 byte = 8 bits)
- If result > 8 bytes (64 bits), storage uses 64-bit cells

**Example formats**:

| Syntax | totalBits | expBits | mantBits | signed | Description |
|--------|-----------|---------|----------|--------|-------------|
| `float<16, 5, 10, true>` | 16 | 5 | 10 | true | 16-bit format |
| `float<16, 7, 8, true>` | 16 | 7 | 8 | true | 16-bit format with wider exponent |
| `float<32, 8, 23, true>` | 32 | 8 | 23 | true | 32-bit IEEE 754 single-precision layout |
| `float<32, 8, 10, true>` | 32 | 8 | 10 | true | TF32-like: 19 value bits in 32-bit slot (13 pad bits) |
| `float<32, 1, 31, true>` | 32 | 1 | 31 | true | 32-bit with minimal exponent |
| `float<64, 11, 52, true>` | 64 | 11 | 52 | true | 64-bit IEEE 754 double-precision layout |
| `float<64, 1, 63, true>` | 64 | 1 | 63 | true | 64-bit with minimal exponent |

**Storage widths** (rounded up to bytes):
- `float<8, ...>` → 8 bits (1 byte) storage
- `float<16, ...>` → 16 bits (2 bytes) storage
- `float<24, ...>` → 32 bits (4 bytes) storage (rounded up)
- `float<32, ...>` → 32 bits (4 bytes) storage
- `float<40, ...>` → 40 bits → rounded to 48 bits → 64 bits (8 bytes) storage
- `float<48, ...>` → 48 bits → rounded to 56 bits → 64 bits (8 bytes) storage
- `float<56, ...>` → 56 bits → rounded to 64 bits (8 bytes) storage
- `float<64, ...>` → 64 bits (8 bytes) storage

**Arithmetic and comparison**:
- Operations on user-defined formats follow the format's bit layout
- Comparisons (<, >, <=, >=, ==, !=) respect the format's precision
- Mixed-format expressions use the wider storage width

**Examples**:

```
// Default 64-bit float
float x = 3.14;

// 16-bit custom format
float<16, 5, 10, true> half;

// 32-bit custom format with unusual exponent
float<32, 1, 31, true> custom32;

// 64-bit IEEE 754 double (explicit layout)
float<64, 11, 52, true> dbl;

// TF32-like: 19 value bits in a 32-bit aligned slot (13 padding bits)
float<32, 8, 10, true> tf32;
tf32 = 1.5;

// Assignment and mixing
float<16, 5, 10, true> a = 1.5;
float<32, 8, 23, true> b = a;  // Widened to 32-bit
float<64, 11, 52, true> c = b;  // Widened to 64-bit
```

**Notes**:
- Precision is explicitly specified via the bit layout tuple, not implicit to a format name
- Users have full control over the exponent/mantissa trade-off for their use case
- `totalBits` may exceed `expBits + mantBits + sign`; the excess bits are padding
- Storage width rounding follows the same rules as integer types per Section 3.2

### 3.4 Boolean type

The keywords bool and boolean both denote the boolean type, whose values are true and false.

### 3.5 Pointer types

A type followed by one or more * or ** tokens denotes a pointer type. int* is a pointer to int; int** is a pointer to a pointer to int. The address-of operator & and the dereference operator * operate on pointers.

### 3.6 Array types

A fixed-size array type is written T[N], where T is an element type and N is a positive integer. Arrays have a fixed size known at compile time. There is no dynamic array allocation in the core language.

An unsized array type T[] is syntactically accepted (for example in type aliases), but its layout is undefined and it cannot be used to declare variables or allocate storage.

### 3.7 String type

The type string denotes a text value. A string value carries its own length in bytes; the length is an intrinsic attribute of the value. Strings support equality and ordered comparison. String and bytes are distinct types.

### 3.8 Bytes type

The type bytes denotes a raw sequence of bytes. Bytes are distinct from strings. A bytes value likewise carries its length.

### 3.9 Byte type

The keyword byte denotes an 8-bit integer type. A byte value occupies one storage byte, and shift operators applied to a byte value use that 8-bit width (section 5.5).

### 3.10 Auto type

The keyword auto denotes a type to be inferred from an initializer. An auto declaration must have an initializer, and the declared binding takes the type of that initializer.

### 3.11 Void type

The keyword void denotes the absence of a type. It is used as a function return type and as the unary void operator, which evaluates its operand and yields no usable value. void is not a valid type for a variable.

### 3.12 Composite types

Struct types are written as a brace-enclosed list of field declarations:

{ field_type field_name ; ... }

Object literals produce struct values:

{ name: expression, ... }

Union types can be written in type annotations using the `|` separator between type names (for example, `int | string`). Union types also arise internally as the result of unifying multiple alternatives. A const union may hold at most one non-nullable member.

### 3.13 Type aliases

A type alias binds a name to a type specification:

type name = type_spec ;

Examples:

type i32 = int<32, signed>;
type u32 = int<32, unsigned>;
type i16 = int<16, signed>;
type byte = int<8, unsigned>;

After the declaration, the alias name may be used wherever its underlying type is expected:

i32 y = 100;

A type alias has file scope and does not introduce a value scope.

### 3.14 Type compatibility

Declaration and assignment check that the initializer or assigned value unifies with the declared type. All numeric kinds (number, byte, and any integer specialization) unify with each other; all floating-point kinds (any float<> specialization) unify with each other; no implicit promotion is performed beyond this numeric compatibility. String unifies only with string, and bytes only with bytes, in direct assignment. Pointers unify when their pointed-to types unify; a pointer to byte is broadly compatible with other pointer types. Arrays unify when element type and length agree.

## 4. Grammar (EBNF)

The grammar below defines the concrete syntax. Terminals are shown in double quotes. { } means zero or more repetitions, [ ] means optional, | separates alternatives.

program ::= { statement }

statement ::= function_declaration
            | type_declaration
            | variable_declaration
            | return_statement
            | if_statement
            | while_statement
            | switch_statement
            | break_statement
            | continue_statement
            | labeled_statement
            | block
            | expression_statement
            | empty_statement

block ::= "{" { statement } "}"

function_declaration ::= return_type IDENTIFIER "(" [ parameter { "," parameter } ] ")" block
return_type ::= type_spec
parameter ::= [ type_spec ] IDENTIFIER | "..."

type_declaration ::= "type" IDENTIFIER "=" type_spec ";"

variable_declaration ::= [ "const" ] type_spec IDENTIFIER [ "[" number "]" ]
                       [ ":" type_spec ] [ "=" initializer ] ";"

return_statement ::= "return" [ expression ] ";"

if_statement ::= "if" "(" expression ")" consequent [ "else" consequent ]
consequent ::= block | statement

while_statement ::= "while" "(" expression [ ";" expression ] ")" consequent

switch_statement ::= "switch" "(" expression ")" "{" switch_case { switch_case } "}"
switch_case ::= "case" expression ":" { statement }
              | "default" ":" { statement }

break_statement ::= "break" [ IDENTIFIER ] ";"
continue_statement ::= "continue" [ IDENTIFIER ] ";"

labeled_statement ::= IDENTIFIER ":" statement

empty_statement ::= ";"

expression_statement ::= expression ";"

type_spec ::= type_name [ int_args | float_args ] [ pointer_suffix ] [ array_suffix ]
type_name ::= IDENTIFIER | "int" | "float" | "string" | "bool" | "boolean"
            | "byte" | "bytes" | "auto" | "void" | "null"
int_args ::= "<" number "," ("signed" | "unsigned") ">"
float_args ::= "<" ">"
             | "<" number "," number "," number "," ("true" | "false") ">"
pointer_suffix ::= { "*" | "**" }
array_suffix ::= "[" number "]"

expression ::= assignment_expression

assignment_expression ::= conditional_expression
                        | conditional_expression assignment_operator assignment_expression
assignment_operator ::= "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^=" | "<<=" | ">>="

conditional_expression ::= logical_or_expression

logical_or_expression ::= logical_and_expression { "||" logical_and_expression }
logical_and_expression ::= inclusive_or_expression { "&&" inclusive_or_expression }
inclusive_or_expression ::= exclusive_or_expression { "|" exclusive_or_expression }
exclusive_or_expression ::= and_expression { "^" and_expression }
and_expression ::= equality_expression { "&" equality_expression }
equality_expression ::= relational_expression { ("==" | "!=") relational_expression }
relational_expression ::= shift_expression { ("<" | ">" | "<=" | ">=") shift_expression }
shift_expression ::= additive_expression { ("<<" | ">>") additive_expression }
additive_expression ::= multiplicative_expression { ("+" | "-") multiplicative_expression }
multiplicative_expression ::= unary_expression { ("*" | "/" | "%") unary_expression }

unary_expression ::= prefix_operator unary_expression | postfix_expression
prefix_operator ::= "!" | "~" | "+" | "-" | "&" | "*" | "typeof" | "void"

postfix_expression ::= primary { call_suffix | member_suffix | index_suffix | range_suffix }
call_suffix ::= "(" [ argument_list ] ")"
member_suffix ::= "." member_name
index_suffix ::= "[" expression "]"
range_suffix ::= "[" expression ":" expression "]"
member_name ::= IDENTIFIER | number_literal | string_literal
argument_list ::= expression { "," expression }

primary ::= "(" expression ")"
          | number_literal
          | string_literal
          | bytes_literal
          | "true" | "false"
          | "null"
          | IDENTIFIER
          | object_literal
          | array_literal

bytes_literal ::= "bytes" string_literal
                | "bytes" number_literal

object_literal ::= "{" [ object_property { "," object_property } ] "}"
object_property ::= member_name ":" expression

array_literal ::= "[" [ array_element { "," array_element } ] "]"
array_element ::= expression | number_literal ":" expression

initializer ::= expression

## 5. Semantics

### 5.1 Program and scopes

A program is a sequence of top-level statements. There are three value scopes: global (file level), function (parameters and locals), and block (inside { }). Type aliases have file scope. Name resolution searches innermost scope outward. An inner declaration shadows an outer one with the same name.

### 5.2 Declarations and variables

A variable declaration binds a name to a type and an optional initializer. With auto, the type is inferred from the initializer and an initializer is mandatory. With an explicit type, the initializer must unify with that type. A declaration without an initializer is zero-initialized for its type (auto requires an initializer, so it cannot be uninitialized).

const declares a read-only binding; assigning to a const binding is an error.

### 5.3 Functions

A function is declared with a return type, a name, a parameter list, and a block body. Parameters are passed by value. A parameter may be untyped (its type is inferred from the argument at the call site) or explicitly typed. A trailing ... parameter denotes variadic arguments. Functions may call themselves recursively. There is no tail-call guarantee.

### 5.4 Control flow

if / else selects a branch based on a condition. while repeats while a condition holds, with an optional step expression evaluated after each iteration: `while (condition; step) { body; }`. When no step is given (`while (condition) { body; }`) it behaves as a plain condition loop. break exits the enclosing loop or switch; continue advances to the next iteration (evaluating step when present, then re-checking the condition). switch evaluates a discriminant and transfers control to the matching case, or to default if none matches. break and continue may carry a label to name an enclosing loop.

Labeled statements (name: statement) associate a label with a statement; labels are used only by break and continue.

### 5.5 Expressions

Expression evaluation is left-to-right for operands. The logical operators && and || short-circuit: the right operand is evaluated only when the result is not already determined.

The unary operator typeof yields a string naming the type of its operand. It returns:

- "float" for a floating-point number
- "int(<width>, signed)" or "int(<width>, unsigned)" for an integer
- "string" for a string
- "boolean" for a boolean
- "byte" for a byte
- "bytes" for bytes
- "null" for null
- "pointer" for a pointer
- "function" for a function
- "object" for a struct, array, or union

The unary operator void evaluates its operand and discards the result.

Comparison operators == and != work for numbers, strings, booleans, and pointers. Ordered comparisons <, >, <=, >= work for numbers and for strings (lexicographic by byte). Bitwise operators &, |, ^, <<, >> operate on integers and yield a number. The operators &, |, ^, and unary ~ use the same storage width as the shifts. All integer values are carried as 64-bit double-precision cells; bitwise operations on values above 2^53 are limited by double precision.

A shift is performed at the storage width of its left operand: 8 bits for byte and for int widths 1 through 8, 16 bits for widths 9 through 16, 32 bits for widths 17 through 32 and for an untyped number, and 64 bits for widths beyond 32. The shift count is reduced modulo that width, so a count of 8 on an 8-bit operand shifts by 0. << fills vacated bits with zeros; >> fills them with zeros when the left operand is unsigned and replicates the sign bit when it is signed. Because integer values are carried as 64-bit double-precision cells, int<64> shift results above 2^53 are limited by double precision.

Compound assignment (for example +=) combines an operation with assignment; the left side must be an assignable target (a variable, a dereference, or a member).

### 5.5.1 Floating-point semantics

All floating-point arithmetic is performed in double precision (64-bit IEEE 754). float<> values of narrower widths are widened to double for computation, narrowed back to their declared width after each operation, and narrowed again on store. This simulates hardware rounding at the declared bit-layout width. Ordered comparisons (<, >, <=, >=, ==, !=) return false when either operand is NaN. Division by zero follows IEEE 754: dividing a nonzero value by zero yields infinity, and dividing zero by zero yields NaN. Signed zero is preserved (0.0 and -0.0 are distinct values but compare equal).

### 5.5.2 ABI and calling convention

All function parameters and return values are passed in 64-bit cells. Integer values are zero-extended or sign-extended to 64 bits according to their signedness. Floating-point values are passed as 64-bit IEEE 754 doubles. Pointers are passed as 64-bit addresses. Variadic functions receive a hidden va_count parameter (number of variadic 64-bit slots) followed by a va_array pointer to an array of 64-bit slots. There is no tail-call guarantee. The calling convention follows the platform's native C ABI as implemented by LLVM; no custom register allocation or stack layout is defined beyond the 64-bit cell convention.

### 5.5.3 Memory model

Memory management is implementation-defined and not part of the core language specification. The reference implementation uses a bump arena allocator: an initial 64 KiB arena is reserved, allocations bump a pointer forward with 16-byte alignment, and when the arena is exhausted, memory is grown via mmap (64 MiB or requested size plus 32 bytes, whichever is larger). The free() builtin is a no-op; all allocated memory is reclaimed at process exit. There is no garbage collection. Stack overflow and out-of-memory conditions terminate the program via the bounds() runtime helper. Stack-allocated local variables use the platform's native stack; their lifetime ends when the enclosing block or function returns.

### 5.6 String semantics

A string value is a length-prefixed sequence of bytes. Its length is known and fixed for the value. Equality compares the byte content; ordered comparison is lexicographic by byte value. String and bytes are distinct types and are not implicitly convertible in the core language.

### 5.7 Initialization and zero values

A variable declared without an initializer is zero-initialized for its type: numbers and booleans to zero or false, strings and bytes to an empty value, pointers and arrays to a null or empty representation, and structs field by field.
