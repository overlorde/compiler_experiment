# compiler_experiment

A minimal C compiler that targets LLVM IR, built as a learning project.

## Prerequisites

- GCC
- LLVM 14 (`llvm-14`, `llvm-14-dev`)

On Ubuntu/Debian:
```bash
sudo apt install gcc llvm-14 llvm-14-dev
```

## Build

From the project root:

```bash
make            # build → build/cc
make -j$(nproc) # parallel build (faster)
make clean      # remove build/
```

The compiler binary lives at `build/cc`.

## Usage

The compiler takes a `.c` file and produces a `.o` object file.
Then you link it with your system compiler to get an executable.

```bash
# compile
./build/cc input.c output.o

# link
cc output.o -o output

# run
./output
echo $?    # check exit code
```

Or use the `run` script to compile, link, and run in one shot:

```bash
./run input.c
```

Works from any directory. Auto-builds the compiler if needed.

## Tests

```bash
make test
```

Runs all tests in `tests/phase*/`. Each test has:
- `<name>.c` — source file
- `<name>.expected` — expected stdout (empty if no output)
- `<name>.exitcode` — expected exit code

## Project Structure

```
src/
  main.c       — CLI: read file → parse → codegen
  lexer.h/c    — Tokenizer (hand-written)
  parser.h/c   — Recursive-descent parser → AST
  ast.h/c      — AST node types (tagged union)
  codegen.h/c  — AST → LLVM IR → object file
tests/
  run_tests.sh — Test harness
  phase1/      — Integer literals + return
  phase2/      — Expressions + operators
  phase3/      — Variables + statements
  phase4/      — Control flow (if/else, while, for)
  phase5/      — Functions, calls, recursion
build/
  cc           — Compiler binary (after make)
  *.o          — Object files
```

## Pipeline

```
source.c → Lexer → Tokens → Parser → AST → Codegen → LLVM IR → .o → cc (linker) → executable
           ~~~~~~           ~~~~~~~~        ~~~~~~~~
           lexer.c          parser.c        codegen.c
```

- **Lexer** turns source text into tokens
- **Parser** turns tokens into an AST (abstract syntax tree)
- **Codegen** walks the AST, calls LLVM-C API to build IR, emits a `.o` file
- **System `cc`** links the `.o` with C runtime startup code to produce an executable

## Supported

- `int` type (32-bit signed)
- Integer literals, arithmetic (`+`, `-`, `*`, `/`, `%`)
- Comparisons (`<`, `>`, `<=`, `>=`, `==`, `!=`)
- Logical operators (`&&`, `||`, `!`), bitwise not (`~`)
- Local variables with assignment and block scoping
- `if`/`else` (including chained else-if)
- `while` loops
- `for` loops (with optional init/cond/post, including `int` declarations in init)
- Nested blocks and nested control flow
- Multiple functions with `int` parameters and `int` return
- Function calls, nested calls, recursion
- Forward references (call a function defined later)
- Forward declarations (`int foo(int x);`)

## Not Supported

- Preprocessor (`#include`, `#define`)
- Pointers, arrays, strings
- Global variables
- Multiple types (`char`, `float`, `void`, structs, enums, unions)
- `switch`, `do-while`, `break`, `continue`, `goto`
- Type casts, `sizeof`
