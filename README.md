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

Or use the shortcut:

```bash
make run FILE=tests/phase2/add.c
```

This builds the compiler (if needed), compiles, links, runs, and prints the exit code.

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
