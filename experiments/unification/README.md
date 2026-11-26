# Unification demo

Run the compiler's actual `type_unify` implementation and watch equality
constraints solve unknown types, propagate through pointers, and reject
incompatible or recursive types.

```sh
make demo-unification
```

The executable also has a quiet verification mode:

```sh
./build/unification_demo --check
```

The demo has no external dependencies beyond a C++17 compiler. Its Makefile
target links `demo.cpp` directly with `src/types.cpp`, without LLVM. `make test`
builds and runs the demo's checks alongside the focused type tests and the
compiler regression suite.

## What the demonstration shows

1. An allocation begins with element type `t0`. The constraint from storing
   `41` solves `t0 = int`, so the pointer becomes `ptr<int>` and its load has
   type `int`.
2. Two allocations keep independent unknowns: one becomes `ptr<int>`, the
   other `ptr<char>`.
3. Constraints propagate through aliases and nested pointers.
4. A boolean store conflicts with an earlier integer constraint. The rejected
   constraint leaves the earlier solution intact.
5. The occurs check rejects both `a = ptr<a>` and the indirect cycle formed
   by `a = ptr<b>` followed by `b = ptr<a>`. The remaining unknown is still
   usable after rejection.
6. Subtyping remains separate: `int <: number` and `ptr<int> <: ptr` still
   hold, but these pairs cannot be unified as equal types.

The first scenario prints:

```text
Allocation starts with p : ptr<t0>
store(p, 41): t0 = int -> solved
  now: int = int
print_int(load(p)): int = int -> solved
  now: int = int
Inferred p : ptr<int>; load(p) : int
```

## API and scope

`TypeUnifyResult type_unify(Type *a, Type *b)` returns `TYPE_UNIFY_OK` on
success, or `TYPE_UNIFY_MISMATCH`, `TYPE_UNIFY_OCCURS`, or
`TYPE_UNIFY_INVALID`. Success has value zero. The solver emits no diagnostics;
the caller can attach a source location or constraint explanation.

On success, the two types compare equal after resolution. On failure, the
call leaves links unchanged. Successful earlier calls stay in effect; there
is no transaction spanning several constraints. Input type graphs must be
acyclic, and the occurs check preserves that invariant. Both structural
descent and occurs checking are iterative for the current unary pointer types.

The demonstration supplies constraints corresponding to memory operations
directly to the type engine. The `.ce` frontend now also generates and solves
constraints from source: it supports omitted function annotations, inferred
`let`/`auto` locals, inferred allocation elements, and polymorphic top-level
function signatures. See the root README for syntax, recursive-group checking,
and conservative region reclamation. The demonstration remains a focused trace
of the equality solver without parsing or generalization.
