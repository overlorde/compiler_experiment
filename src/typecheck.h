#ifndef TYPECHECK_H
#define TYPECHECK_H

#include "ast.h"
#include "symtab.h"

/*
 * Static type checker.
 *
 * Two passes over the program:
 *   1. construct: register every function signature in func_env, so calls
 *      (and mutual recursion) resolve regardless of definition order.
 *   2. check: walk each function body bottom-up, binding locals into
 *      var_env scopes and applying the typing rules.
 *
 * Errors are reported through error.h (error_at); the return value is the
 * number of errors found -- 0 means well typed, nonzero means the caller
 * must NOT proceed to codegen.
 */
int typecheck(ASTNode *program,
              SymTab *type_env,   /* type names -> Type   (built-ins seeded by caller; unused for now) */
              SymTab *func_env,   /* function names -> function ASTNode* (filled by pass 1) */
              SymTab *var_env,    /* root variable scope; per-function/block scopes chain off it */
              SymTab *op_env);    /* operator overloads (unused for now) */

/* Seed func_env with built-in (extern) function signatures: print_int,
 * print_bool. The synthetic ASTNodes have body == NULL; their bodies live
 * in runtime/runtime.c and are resolved at link time. Call this once,
 * before typecheck, alongside the type_env seeding in main. */
void typecheck_register_builtins(SymTab *func_env);

#endif
