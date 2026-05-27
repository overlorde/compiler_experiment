# C-like Compiler with LLVM Codegen Support

A C-like compiler written in C, with a hand-written lexer, recursive-descent parser, tagged-union AST, and LLVM IR code generation via the `llvm-c` API (LLVM 18), emitting native object files.

## Highlights

- **Full front-to-back pipeline** — hand-written lexer, recursive-descent parser, tagged-union AST, and LLVM IR codegen via the `llvm-c` API (LLVM 18), producing native object files.
- **Real language subset** — functions with parameters, `int`/`bool` types, arithmetic/comparison/logical operators, `if`/`while`/`for`, nested blocks, returns, and calls.
- **Nominal type system with type erasure** — declared types are threaded from the parser through the AST, while all types uniformly lower to `i32` at the codegen boundary.
- **Lexically-scoped symbol tables** — built on a parent-pointer chain, giving correct name resolution and inner-scope shadowing.
- **"Construct-first, query-later" scope pass** — walks the AST once to assemble a scope tree, annotating each function/block node with its scope so later passes resolve names in O(1).
- **Function-body vs. nested-block scopes** — distinguished via a one-shot "pending scope" technique, so function parameters and top-level body locals correctly share one scope, matching C semantics.
- **Modular inspection layer** — a renderer-callback design decouples scope traversal from value formatting, rendering the scope tree top-down with depth-based indentation.
- **Architected for extensibility** — built toward a type-checking playground where typing rules (Γ ⊢ e : τ) are the editable surface and the supporting infrastructure stays out of the way.
