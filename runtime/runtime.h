/* runtime.h — the exported ABI of the runtime.
 *
 * Every symbol declared here is one the compiler may emit a call to.
 * This header is the single written record of the compiler<->runtime
 * contract; keep it in sync with:
 *
 *   - codegen pass 1a extern declarations (src/codegen.cpp, ~line 411)
 *   - builtin signatures registered for the typechecker
 *     (src/typecheck.cpp, typecheck_register_builtins)
 *
 * Everything is `int64_t` by design: the front end erases all source
 * types (int, bool, char, number) to one i64 machine word — wide enough
 * to also carry an address once pointers arrive. Names are unmangled
 * (extern "C") because codegen emits literal symbol names like
 * @print_int.
 */
#ifndef RUNTIME_H
#define RUNTIME_H

#include <cstdint>

extern "C" {

int64_t print_int(int64_t x);
int64_t print_bool(int64_t x);
int64_t print_char(int64_t x);

/* Typed heap primitives. The compiler infers/checks their phantom elements;
 * the runtime tracks allocation regions and leaves bounds unchecked. */
int64_t alloc(int64_t n);
int64_t load(int64_t p);
int64_t store(int64_t p, int64_t v);
int64_t offset(int64_t p, int64_t i);   /* address of cell i: p + 8*i */

/* Compiler-only region boundaries, emitted around every user function.
   policy: 0 = local, 1 = caller allocation region, 2 = program root. */
int64_t __ce_region_enter(int64_t policy);
int64_t __ce_region_leave();

} // extern "C"

#endif /* RUNTIME_H */
