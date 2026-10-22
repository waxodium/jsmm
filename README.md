# JS--

<table width="100%">
    <tr>
        <td bgcolor="#f6f8fa" style="padding: 15px; border: 1px solid #d0d7de;">
            !! <b>Note:</b> This Engine isn't finished. Contributing may not be accepted
        </td>
    </tr>
</table>

[/dʒeɪ ɛs ɛm ɛm/](https://ipa-reader.com/?text=%2Fd%CA%92e%C9%AA%20%C9%9Bs%20%C9%9Bm%20%C9%9Bm%2F) can also be 
**JS--/JavaScript--** or JavaScript decrement

"**JSMM**" is a non-standard and statical subset of JavaScript programming language. 
 
Source code of **Compiler Engine for JS minus-minus**

jsmm took features concepts from [edon.js](https://github.com/waxodium/edon), 
a dynamic JavaScript runtime that provides APIs for interacting with C.

[![License: MPL 2.0](https://img.shields.io/badge/License-MPL_2.0-brightgreen.svg)](./LICENSE)
[![Platform: Linux x86_64](https://img.shields.io/badge/platform-Linux_x86_64-blue.svg)](#)
[![Syntax: ES2015](https://img.shields.io/badge/syntax-ES2015-yellow.svg)](#)


## About

*jsmm* uses **ES2015 (ECMAScript 2015) as its base syntax**. 
It does not introduce a separate programming language or modify JavaScript syntax. 
Instead, it restricts the supported ECMAScript features to a statical subset suitable for native compilation.

The compiler uses implicit type inference and static analysis to reject unsupported or unsafe dynamic features at compile time.


*jsmm* excludes JavaScript built-ins, those includes `console`, `arrays`, `objects`

(The compiler provides a single `cat()` function for concatenation. This engine is only compatible with __x86_64 linux__)


| Operating System | Arch | Standing |
| :--- | :--- | :--- | 
| Linux | x86_64 | Supported | 

## Build
Make JavaScript-decrement compiler as CLI program:

```bash
make
./build/jsd # executable
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

*jsmm* is licensed under the terms of the [Mozilla Public License 2.0](./LICENSE). Review `LICENSE` file for details.
