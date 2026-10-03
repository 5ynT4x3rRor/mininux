# TinyC MiniNux

TinyC compiles a freestanding subset of C into a small stack bytecode and runs
it in a bounded interpreter (no native machine code execution). It supports a
single `int main()` function whose body is one `return` expression:

```c
int main() { return (2 + 3) * 4 - 7 / 2; }
```

The function can also contain up to eight initialized local `int` declarations
before its return statement. Expressions support decimal integer literals,
previously declared variables, parentheses, unary minus, and `+`, `-`, `*`,
`/` with C-like precedence. Signed integer overflow wraps to 32 bits;
division by zero is reported at runtime. Assignment after declaration,
function calls, strings, loops, and multiple functions are not supported yet.

In the MiniNux terminal, save a source file with `write prog.c int main() { return 42; }`
then run `tinyc prog.c`. Source files use the existing 224-byte persistent-file
limit. TinyC compiles and executes the supported subset and prints `main()`'s
result. The same compiler and interpreter are linked into the BIOS kernel and
the UEFI terminal.
