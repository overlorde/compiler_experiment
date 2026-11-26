# compiler_experiment

A compiler for **ce**, a small language of my own, written in C++ as a learning project. ce borrows C's surface syntax but is not C — it's strictly nominally typed (no truthiness, no char/int punning, `bool` is not an integer) with phantom-typed pointers. The front end is hand-written (lexer, recursive-descent parser, tagged-union AST), and the back end emits LLVM IR through the LLVM C++ API (LLVM 18), which clang then turns into a native binary. Source files use the `.ce` extension.

This is not meant to be a serious language. It's a place for me to actually implement type-system ideas instead of just reading about them. The long-term goal is for the typing rules to be the interesting part of the codebase, with the supporting infrastructure (scoping, codegen, the runtime boundary) boring and out of the way.

## What works today

The language has functions with parameters, `int`, `bool`, `number`, `char` and `ptr<T>` types, `true`/`false`, the usual arithmetic, comparison and logical operators, `if`/`while`/`for`, nested blocks, returns, and calls. Subtyping is in: `int` is a subtype of `number`, and the subsumption check is threaded through one helper so every typing rule respects it without needing a special case.

Heap memory works through four runtime primitives — `alloc`, `load`, `store`, `offset` — with phantom-typed pointers on top: `ptr<int>` and `ptr<char>` are distinct types to the checker and the same bare word at runtime, so `load` on a `ptr<int>` gives back an `int` and storing the wrong element type is a compile error. `alloc(n)` starts with an unknown element type, inferred from its uses; `alloc<T>(n)` supplies it explicitly. An explicit bare `ptr` forgets the element and retains integer load/store behavior; it cannot convert back to a typed pointer. Bounds and initialization are deliberately unchecked.

Function signatures now support Hindley–Milner-style inference and polymorphism. Return and parameter annotations may be omitted, `auto` introduces a fresh unknown wherever a type is accepted, and `let` declares an inferred mutable local:

```ce
identity(x) { return x; }

main() {
    print_int(identity(42));
    print_bool(identity(true));
    print_char(identity('A'));
    let p = alloc(8);
    store(p, 7);
    print_int(load(p));
    return 0;
}
```

The same identity definition works at independent types. Aliases of `p` share its element unknown, so storing a character after storing an integer is rejected. `let` and `auto` locals stay monomorphic, including allocations; only top-level function signatures are generalized. Functions are first-order: there are no lambdas, function-valued variables, or higher-order parameters yet.

All types lower to a 64-bit integer in LLVM. Characters, booleans, pointers, integers — the typechecker keeps them apart, codegen treats them uniformly. The idea is to do as much work as possible at compile time so the emitted code stays trivial.

Codegen itself is intentionally naive. Every local is a stack allocation, every variable access is a load and a store. I rely on LLVM's `mem2reg` pass to turn those allocations into SSA registers, which is what makes the rest of the standard `-O1` pipeline (instruction combining, GVN, control-flow simplification, register allocation) actually do useful work downstream.

## Scoping

Name resolution runs in a separate pass before typechecking. It walks the AST once, builds a tree of lexical scopes (parent-pointer symbol tables), and attaches the right scope to each function, block, and `for` node. The typechecker reads those scopes and tracks which declarations have become visible: an initializer sees enclosing bindings, and a local becomes visible afterward. References before declaration and duplicate names in one scope are rejected. A `for` initializer stays inside the loop's scope.

There's a small subtlety about function-body vs. nested-block scopes — a function's parameters and its top-level locals need to share one scope so that `int main(int x) { int x = 5; }` is a redeclaration error, not a fresh inner binding. I handle this with a "pending scope" flag the function emits and the next block consumes, instead of always opening a new scope on `{`.

## The typechecker

The checker is one switch over `NodeKind` where each case lines up with a typing rule. At a must-match premise, it preserves an existing subtype conversion or solves an equality using mutable type-variable links and an occurs check. This is an Algorithm J style implementation. Subtyping remains an extension outside plain HM: inference does not solve arbitrary subtype inequalities or promise principal types for programs mixing those conversions. Use explicit annotations when a widened `number` or bare `ptr` is intended.

The call graph is checked in dependency order. Every mutually recursive group shares monomorphic signatures while its bodies are checked; afterward, signature variables absent from the surrounding environment are quantified. Each outside call instantiates those variables with a consistent fresh copy. This preserves forward calls and mutual recursion without permitting inferred polymorphic recursion. Built-ins such as `print_int` remain ordinary synthesized signatures; memory primitives generate pointer constraints directly.

## Region memory

Every generated user function enters a region and leaves it on every return or fallthrough. The runtime frees the region's blocks together; source programs need no `free` or garbage collector. A post-inference pass chooses one of three allocation destinations:

- A function with a scalar result and no pointer-publication effects allocates locally. Its temporary blocks are reclaimed when it returns.
- A function that may return a pointer allocates into its caller's allocation region, so returned blocks and offsets into them survive the call. This also covers generic results that could be pointers.
- A function that may store a pointer into heap memory uses the program root region. This effect propagates to every transitive caller, keeping both published pointers and their containers alive.

This is a conservative implementation of function-scoped regions, not the full Tofte–Talpin region calculus. Pointer-publication workloads, including a generic store whose value could be a pointer, may retain allocations until program exit. Allocations inside one long-running function are also retained until its region exits. Finer region/effect polymorphism and block/loop region inference remain future work. Bounds checking, definite initialization, and a general memory-safety theorem are outside the current guarantee.

## What's next

First-order polymorphic function inference and conservative automatic region reclamation are implemented. The remaining type-system work includes ADTs with pattern matching, higher-order functions, and more precise region/effect inference.

## Building

```
make          # build the compiler
make test     # run the regression suite (pass + fail + differential IR)
make test-region-sanitizers  # ASan/UBSan, both emitters, repeated entry + cleanup
make wasm     # Emscripten; or set EMCC=/path/to/emcc
make test-wasm # Node: release identity, checksums, all source regressions
./build/cc path/to/program.ce /tmp/program.o
```

Source regressions are bash-driven: each test is a `.ce` file plus an `.expected` file (stdout for pass tests, an error-message regex for fail tests). `make test` also runs direct type/scheme, frontend, and region-runtime checks. The optional sanitizer target checks inferred and region programs through textual IR and optimized LLVM IR, invokes each entry point twice, and asserts that all runtime regions and blocks have been reclaimed.

`VERSION` names the playground release. The Wasm build exports `ce_version()`
and `ce_build_info()` and writes `build/version.json`, recording the compiler
commit, a source fingerprint (including uncommitted source changes), runtime
ABI, and SHA-256 checksums of both artifacts. Release CI requires the tag to
match `VERSION`, builds with pinned Emscripten 6.0.3, verifies the embedded
identity, and publishes all three files together.

## Layout

```
src/        the compiler
runtime/    the runtime linked into compiled programs (C++, extern "C")
tests/      regression suite
writing/    notes, including the cover-letter writeup
```
