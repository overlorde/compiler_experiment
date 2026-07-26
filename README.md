# compiler_experiment

A compiler for **ce**, a small language of my own, written in C++ as a learning project. ce borrows C's surface syntax but is not C — it's strictly nominally typed (no truthiness, no char/int punning, `bool` is not an integer) with phantom-typed pointers. The front end is hand-written (lexer, recursive-descent parser, tagged-union AST), and the back end emits LLVM IR through the LLVM C++ API (LLVM 18), which clang then turns into a native binary. Source files use the `.ce` extension.

This is not meant to be a serious language. It's a place for me to actually implement type-system ideas instead of just reading about them. The long-term goal is for the typing rules to be the interesting part of the codebase, with the supporting infrastructure (scoping, codegen, the runtime boundary) boring and out of the way.

## What works today

The language has functions with parameters, `int`, `bool`, `number`, `char` and `ptr<T>` types, the usual arithmetic, comparison and logical operators, `if`/`while`/`for`, nested blocks, returns, and calls. Subtyping is in: `int` is a subtype of `number`, and the subsumption check is threaded through one helper so every typing rule respects it without needing a special case.

Heap memory works through four runtime primitives — `alloc`, `load`, `store`, `offset` — with phantom-typed pointers on top: `ptr<int>` and `ptr<char>` are distinct types to the checker and the same bare word at runtime, so `load` on a `ptr<int>` gives back an `int` and storing the wrong element type is a compile error. Bounds are deliberately unchecked (the C road, chosen knowingly).

All types lower to a 64-bit integer in LLVM. Characters, booleans, pointers, integers — the typechecker keeps them apart, codegen treats them uniformly. The idea is to do as much work as possible at compile time so the emitted code stays trivial.

Codegen itself is intentionally naive. Every local is a stack allocation, every variable access is a load and a store. I rely on LLVM's `mem2reg` pass to turn those allocations into SSA registers, which is what makes the rest of the standard `-O1` pipeline (instruction combining, GVN, control-flow simplification, register allocation) actually do useful work downstream.

## Scoping

Name resolution runs in a separate pass before typechecking. It walks the AST once, builds a tree of lexical scopes (parent-pointer symbol tables), and attaches the right scope to each function and block node. The typechecker then just reads `node->scope` when it needs to look something up — no scope construction in the checker itself.

There's a small subtlety about function-body vs. nested-block scopes — a function's parameters and its top-level locals need to share one scope so that `int main(int x) { int x = 5; }` is a redeclaration error, not a fresh inner binding. I handle this with a "pending scope" flag the function emits and the next block consumes, instead of always opening a new scope on `{`.

## The typechecker

The checker is one switch over `NodeKind` where each case lines up with a typing rule. I want the rules themselves to be the editable surface, so adding a new construct should mean adding a case to that switch and not much else. Compiler built-ins like `print_int` are registered as synthesized function AST nodes in the same environment as user functions, which means the call-typing rule handles them with no special case at all.

## What's next

The memory substrate is done: allocation, typed pointers, indexed read/write via `offset`. Phantom types are in.

The remaining type-system work: Hindley-Milner inference, ADTs with pattern matching (which will bring real records and arrays), and region-based deallocation in the Tofte–Talpin style so the source language never needs a manual `free`.

## Building

```
make          # build the compiler
make test     # run the regression suite (pass + fail + differential IR)
./build/cc path/to/program.ce
```

The test suite is bash-driven: each test is a `.ce` file plus an `.expected` file (stdout for pass tests, an error-message regex for fail tests). It's the thing I lean on hardest when changing the typechecker or codegen.

## Layout

```
src/        the compiler
runtime/    the runtime linked into compiled programs (C++, extern "C")
tests/      regression suite
writing/    notes, including the cover-letter writeup
```
