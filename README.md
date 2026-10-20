# rma

**The JavaScript Compiler Engine**

**RMA (Runtime Machine Assembler)** is a deterministic Ahead-of-Time (AOT) compiler engine for a statically analyzable subset of JavaScript.

RMA is designed to be distributed as a compilation feature of [edon.js](https://github.com/waxodium/edon), 
a dynamic JavaScript runtime that provides APIs for interacting with C.

## About

RMA uses **ES2015 (ECMAScript 2015) as its base syntax**. 
It does not introduce a separate programming language or modify JavaScript syntax. 
Instead, it restricts the supported ECMAScript features to a statically analyzable subset suitable for native compilation.

RMA uses implicit type inference and static analysis to reject unsupported or unsafe dynamic features at compile time.


RMA excludes JavaScript built-ins and standard library, those includes `console`, `arrays`, `objects`

The compiler provides a single `cat()` function for concatenation. This engine is only compatible with __x86_64 linux__

## Build
For users who required an rma as CLI program:

```bash
make
./build/rma // executable
```


## License

RMA is licensed under the terms of the [Mozilla Public License 2.0](./LICENSE). Review `LICENSE` file for details.
