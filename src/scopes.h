#ifndef SCOPES_H
#define SCOPES_H

#include "ast.h"
#include "symtab.h"

/*
 * Name-resolution pass: walks the AST once, builds the tree of lexical
 * scopes (parent-pointer symbol tables), and attaches the right scope to
 * each function and block node for later passes to read back.
 *
 * `pending` carries a function's scope down to its body block: non-NULL
 * means "the next block you hit IS this function's body -- put its locals
 * here instead of opening a new scope". It's consumed once, so only the
 * function body reuses it; every other block is genuinely nested. Callers
 * start with scopes_build(ast, &var_env, NULL).
 */
void scopes_build(ASTNode *n, SymTab *env, SymTab *pending);

#endif
