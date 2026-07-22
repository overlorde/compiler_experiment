# stdlib

Standard library written in the source language itself, compiled by
`build/cc` and shipped with user programs — unlike `runtime/`, which is
foreign (C++) code for things the language cannot express.

Boundary rule: code lives here unless it needs a syscall, a libc call, or
compiler-emitted glue — those go in `runtime/` as primitives.

First planned module: `arena` (Phase 2), built on the `__raw_alloc` /
`__raw_free` primitives.
