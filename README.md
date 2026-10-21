# rMa

<table width="100%">
    <tr>
        <td bgcolor="#f6f8fa" style="padding: 15px; border: 1px solid #d0d7de;">
            !! <b>Note:</b> This program isn't finished. contributing may not be accepted
        </td>
    </tr>
</table>


**Compiler Engine for a programming language with ES5 syntax**

**rMa (Runtime Machine Assembler)** Ahead-of-Time (AOT) compiler engine for a 
statically analyzable subset of JavaScript.

RMA is designed to be distributed as a compilation feature of [edon.js](https://github.com/waxodium/edon), 
a dynamic JavaScript runtime that provides APIs for interacting with C.

[![License: MPL 2.0](https://img.shields.io/badge/License-MPL_2.0-brightgreen.svg)](./LICENSE)
[![Platform: Linux x86_64](https://img.shields.io/badge/platform-Linux_x86_64-blue.svg)](#)
[![Syntax: ES2015](https://img.shields.io/badge/syntax-ES2015-yellow.svg)](#)


## About

RMA uses **ES2015 (ECMAScript 2015) as its base syntax**. 
It does not introduce a separate programming language or modify JavaScript syntax. 
Instead, it restricts the supported ECMAScript features to a statically analyzable subset suitable for native compilation.

RMA uses implicit type inference and static analysis to reject unsupported or unsafe dynamic features at compile time.


RMA excludes JavaScript built-ins, those includes `console`, `arrays`, `objects`

The compiler provides a single `cat()` function for concatenation. This engine is only compatible with __x86_64 linux__


| Operating System | Arch | Standing |
| :--- | :--- | :--- | 
| Linux | x86_64 | Supported | 

## Build
Make rMa compiler as CLI program:

```bash
make
./build/rma # executable
```

## Sample
```js
function a(b) {
    for(let i = 1; i<=10; i++) {
        cat(i, ".", b)
    }
}

a("Hello, World");

```

## License

RMA is licensed under the terms of the [Mozilla Public License 2.0](./LICENSE). Review `LICENSE` file for details.

