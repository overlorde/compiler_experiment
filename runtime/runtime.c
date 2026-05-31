/* runtime.c — companion runtime for programs produced by this compiler.
 *
 * Compiled separately into runtime.o; linked alongside the user's compiled
 * .o to provide implementations for the externs the compiler emits.
 *
 * Signatures must match the LLVM declarations the compiler emits in
 * codegen pass 1 AND the synthetic ASTNode signatures registered in
 * func_env. `bool` lowers to i32 throughout the front end, so print_bool
 * takes plain int here to match the ABI. */

#include <stdio.h>

int print_int(int x) {
    printf("%d\n", x);
    return 0;
}

int print_bool(int x) {
    puts(x ? "true" : "false");
    return 0;
}
