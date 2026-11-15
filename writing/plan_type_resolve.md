# plan_type_resolve

Add a read-only operation that follows solved type variables to their current representative. Then make existing type readers use that operation so a linked variable behaves like its resolved type throughout checking and diagnostics.

Implement this plan first, then [plan_type_equal](plan_type_equal.md). These plans prepare the type representation for inference; unification, occurs checks, omitted annotations, and polymorphic schemes come afterward.

## 1 Define the contract in src/types.h

Declare two overloads, `Type *type_resolve(Type *t)` and `const Type *type_resolve(const Type *t)`, so callers preserve constness without casts.

Specify these rules:

- A null input returns null.
- A concrete type returns itself.
- An unresolved `TYPE_VAR` with a null `link` returns itself.
- A linked variable follows `link` repeatedly until reaching a concrete type or unresolved variable.
- Resolution follows only variable links. Resolving `ptr<t0>` returns that pointer node; inspecting its element requires a separate resolution of `param`.
- Resolution never assigns links, compresses paths, allocates, or changes variable IDs.

Types must contain no cycles through variable links or pointer parameters. Future unification must enforce this invariant with an occurs check; this reader does not repair malformed graphs. Keep `var_id` as a diagnostic label and `link` as the eventual solver's state.

## 2 Implement traversal in src/types.cpp

Implement iterative traversal, checking for null before reading the kind and following a link only when the current node is `TYPE_VAR` and its link is non-null. Iteration avoids stack growth for long chains. Both overloads must have the same behavior.

Keep pointer constructors and fresh-variable allocation as they are in this step. Tests will connect links explicitly because the production solver does not exist yet.

## 3 Resolve types at reader boundaries in src/types.cpp

Update these readers in order:

1. `type_equals`: retain its null rejection and resolve both operands before comparing their kinds. Recursive calls will resolve pointer elements. Distinct unresolved variables still need the identity correction in `plan_type_equal`; normalization alone does not fix that bug.
2. `type_subtype`: resolve both inputs before equality and before inspecting kinds or pointer parameters. Preserve `int <: number`, `ptr<T> <: ptr`, and the rejection of conversions from bare pointers to typed pointers.
3. `type_name`: resolve its input before consulting `display` or inspecting its kind. A variable linked to `int` must print `int`, while an unresolved variable keeps its own name such as `t0`.

Pointer diagnostics need an additional change: a pointer's cached display can become stale when its element variable is linked. Render pointer names recursively from their current elements and intern the resulting strings in process-lifetime storage. Preserve the existing `const char *` API and stable returned text: formatting a second argument must not overwrite the first argument's name. Do not mutate the pointer's original cached name to accomplish this.

The pointer constructor's interning policy is addressed separately in `plan_type_equal`; it must not become dependent on mutable semantic equality.

## 4 Update pointer inspection in src/typecheck.cpp

Normalize the pointer argument immediately after checking it in the special rules for `load`, `store`, and `offset`, before reading `kind` or `param`.

- `load` selects the resolved pointer's element type, with the existing integer fallback for a bare pointer.
- `store` selects that same element type as its expected value type.
- `offset` preserves the resolved pointer type.

Keep `expect()` using subtyping. An unresolved variable remains unresolved; these edits must not infer or assign a type to it. A returned element can remain a variable node because subsequent readers resolve it when needed.

## 5 Check dependent modules

`env_print.cpp` already calls `type_name`, so it receives corrected names through that API. The parser continues producing explicit concrete types. Scope construction continues storing `Type *` values. The AST, native emitter, textual emitter, runtime, CLI, and WebAssembly entry point need no changes for resolution.

In the edited modules, distinguish `Type::kind` reads from `ASTNode::kind` reads; only the former need type resolution.

## 6 Add direct tests and build integration

Add `tests/types_test.cpp` as a small native executable linked with `src/types.cpp`. Add a Makefile target for it and run it from `make test` alongside the existing shell suite. Make the target depend on `src/types.h` so representation edits rebuild it. This executable needs C++17 but no LLVM linkage.

Cover:

1. Null inputs through both overloads, concrete types, and fresh unresolved variables.
2. A variable linked to a concrete type and a chain ending at another unresolved variable.
3. Long chains, repeated resolution, const access, and unchanged intermediate links.
4. Root-only resolution of `ptr<t0>`, followed by explicit resolution of its element.
5. Subtyping through linked variables in both allowed and rejected directions.
6. Names before and after linking, including nested pointers and two retained name results.

Then run `make test`. The baseline compiler suite has 99 passing cases, including differential checks between both emitters. Run `make wasm` when Emscripten is available to validate the shared frontend build.

## Completion criteria

Every existing type reader affected by linked variables sees the representative before inspecting its kind. Resolution is read-only, diagnostics reflect linked types, and existing source programs retain their behavior. Completing `plan_type_equal` is required before using fresh variables as distinct pointer element types.

## Implementation and validation

Completed together with `plan_type_equal`. Both const-preserving overloads are implemented, type readers resolve their inputs, pointer names reflect current elements, and the special pointer rules normalize their arguments.

`make test` passed all 10,670 focused type checks and all 99 compiler regressions. The focused executable also passed AddressSanitizer and UndefinedBehaviorSanitizer. A native build of the shared frontend passed four linked-pointer checker cases and three successive `ce_compile` calls covering success, failure, and recovery. The WebAssembly build could not be run because Emscripten is unavailable.
