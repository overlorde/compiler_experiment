# plan_type_equal

Make equality distinguish independent unknowns while recognizing types that resolve to the same structure. Fix pointer interning so constructing a type never merges two independent inference variables.

Implement this after [plan_type_resolve](plan_type_resolve.md). The implementation function remains named `type_equals`; this plan uses the requested document name `plan_type_equal`.

## 1 Define equality in src/types.h

Replace the current primitive-only equality comment with the complete contract:

- Equality reads types without solving them or assigning links.
- A null operand makes equality false, including two null operands.
- Resolve both operands before comparing them.
- The same resolved node equals itself.
- Different unresolved variables are unequal, even though both have kind `TYPE_VAR`.
- Concrete primitive types are equal exactly when their kinds match.
- Bare `ptr` equals bare `ptr`; it does not equal a parameterized pointer.
- Two parameterized pointers are equal exactly when their element types are recursively equal.

Use the unresolved variable node's identity, not its display name, to distinguish unknowns. Variable IDs remain useful for diagnostics. Equality and subtyping remain separate operations: `int` is a subtype of `number`, but they are unequal.

## 2 Implement the cases in src/types.cpp

Apply the comparison steps in this order:

1. Reject null operands.
2. Resolve both operands using `type_resolve`.
3. Return true for identical resolved pointers.
4. Return false for different kinds.
5. Return false for two remaining `TYPE_VAR` nodes, since the identity case already handled the same variable.
6. For `TYPE_PTR`, handle missing parameters explicitly, then recurse when both parameters exist.
7. Return true for matching concrete primitive kinds, handling the current kinds explicitly.

Do not infer that two unresolved variables must be the same because a caller compares them. Equality observes the graph; a future unifier will change it.

Examples: independent `a` and `b` compare unequal; after `a->link = b`, they compare equal. If each independently resolves to `int`, both variables compare equal to `int`. Likewise, `ptr<a>` becomes equal to `ptr<int>` after `a` resolves to `int`.

## 3 Separate pointer construction from equality in src/types.cpp

The current `type_ptr_of()` searches its cache using `type_equals()`. This already collapses pointers to distinct fresh variables because all `TYPE_VAR` nodes currently compare equal. Once links become mutable, semantic equality is also an unstable cache key.

Key the cache by the immediate element node's `Type *` identity instead. Store the original element pointer without resolving it at construction time:

- Repeated `type_ptr_of(a)` calls return the same pointer node.
- `type_ptr_of(a)` and `type_ptr_of(b)` stay distinct when `a` and `b` are different variables.
- Linking variables afterward does not change either constructor key or overwrite either pointer's element.
- Two distinct pointer nodes may later compare equal structurally; clients must use `type_equals` when asking semantic equality.
- Concrete pointer interning continues to work because primitive types and recursively constructed concrete pointers already have stable identities.

Update the cache comments and the `type_ptr_of()` declaration comment to state this distinction. Pointer names use the dynamic rendering introduced by `plan_type_resolve`.

## 4 Preserve subtype behavior in src/types.cpp

Check `type_subtype()` against the new equality behavior. Its equality case should accept the same unresolved variable and variables with equal resolved types, while rejecting unrelated unresolved variables.

Preserve the explicit concrete subtype edges. In particular, `ptr<int>` and `ptr<number>` remain unequal and neither is a subtype of the other; both can still be forgotten to bare `ptr`. Comparing variables must never create an implicit subtype relationship or bind a variable.

## 5 Audit callers module by module

- `typecheck.cpp`: `expect()` continues to call `type_subtype`. The load, store, and offset rules use resolution from the first plan; they need no new equality-specific branches.
- `parser.cpp`: pointer syntax keeps calling `type_ptr_of`; no grammar change is needed.
- `env_print.cpp`: diagnostics keep calling `type_name`; equality has no printing responsibility.
- `scopes.cpp` and `symtab.cpp`: bindings keep their original `Type *` nodes so later links remain visible through resolution.
- `ast.h`, both emitters, the runtime, `main.cpp`, and `wasm_api.cpp`: no equality-specific edits are needed. Machine representation remains unchanged.

## 6 Extend tests/types_test.cpp

Use the direct type test executable and Makefile integration from `plan_type_resolve`. These cases cannot be expressed through current `.ce` syntax because the parser does not construct inference variables.

Cover:

1. Every primitive against itself and different primitive kinds; verify that `int` and `number` remain unequal.
2. Null operands, same-variable equality, and inequality between fresh variables.
3. Variable aliases, chains, separate variables resolved to the same concrete type, and variables resolved to different types.
4. Bare pointers, parameterized pointers, nested pointers, and matching or differing element types.
5. Pointer cache identity before and after links change. Construct `ptr<a>` and `ptr<b>` before linking, then verify that their stored elements remain distinct even if they later compare equal.
6. Equality symmetry, transitivity for well-formed resolved types, repeated calls, and unchanged links after comparison.
7. The existing subtype directions and rejection of variance between typed pointers.

Run `make test` to execute the focused type tests and the full compiler suite. The existing phantom-pointer failure cases must continue rejecting bare-to-typed conversion, pointer laundering, and mismatched stores. Run the WebAssembly build when its toolchain is available.

## Completion criteria

Fresh variables retain independent identities, linked variables compare through their representatives, structural pointer equality works recursively, and constructor caching preserves the original unknowns. All focused and existing regression tests pass. The next implementation milestone is unification with an occurs check.

## Implementation and validation

Completed together with `plan_type_resolve`. Equality now distinguishes unresolved variables by node identity, follows resolved representatives, and compares pointer structure recursively. Pointer constructors cache by original element identity, preserving independent unknowns as links change. Existing subtype directions remain intact.

`make test` passed all 10,670 focused type checks and all 99 compiler regressions, including the pointer-laundering and mismatched-store rejection cases. The focused executable also passed AddressSanitizer and UndefinedBehaviorSanitizer. Native frontend checks passed for linked pointers and successive playground API calls. The WebAssembly build could not be run because Emscripten is unavailable.
