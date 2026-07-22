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
 * Everything is `int` by design: the front end erases all source types
 * (int, bool, char, number) to i32, so print_bool/print_char take plain
 * int to match the ABI. Names are unmangled (extern "C") because codegen
 * emits literal symbol names like @print_int.
 */
#ifndef RUNTIME_H
#define RUNTIME_H

extern "C" {

int print_int(int x);
int print_bool(int x);
int print_char(int x);

} // extern "C"

#endif /* RUNTIME_H */
