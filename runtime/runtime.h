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

} // extern "C"

#endif /* RUNTIME_H */
