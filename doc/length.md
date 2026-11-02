# The `.length` Property

`.length` reports the size of a value. It is a read-only property, not a method.

```js
string text = "hello";
number size = text.length;
```

Use `value.length`, not `value.length()`.

## Meaning by type

| Type | `.length` means | Example |
| --- | --- | --- |
| `string` | UTF-8 byte length, not character count | `"hello".length == 5` |
| `bytes` | Byte count | `(bytes "AB").length == 2` |
| `array` | Element count | `values.length == 3` |

For strings, non-ASCII characters can use more than one byte:

```js
"\u{1F600}".length == 4
```

## Arrays

```js
number values[3] = [10, 20, 30];
number size = values.length;

number sparse[4] = [0: 5, 3: 9];
number sparseSize = sparse.length;

auto grid = [[1, 2], [3, 4]];
number rows = grid.length;
number columns = grid[0].length;
```

`values.length` is `3`, `sparse.length` is `4`, `rows` is `2`, and `columns` is `2`.

This works for arrays passed to functions and arrays returned from functions as well.

## Read-only

Assigning to `.length` is a compile-time error:

```js
number values[2] = [1, 2];
values.length = 5;
```

```
jsmm: invalid assignment target: length
```

Updating it is also rejected:

```js
values.length++;
```

```
jsmm: invalid assignment target: length
```
