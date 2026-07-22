/* scopes.cpp -- the name-resolution pass, extracted from main.cpp so both
 * the CLI and the wasm entry point can run it. See scopes.h for the
 * `pending` contract. */

#include <assert.h>
#include <stdlib.h>

#include "scopes.h"

void scopes_build(ASTNode *n, SymTab *env, SymTab *pending) {

    if (!n) {
        return;
    }
    assert(env);   /* even at the root we pass &var_env -- a NULL would be a bug */

    switch (n->kind) {
        case NODE_PROGRAM:
            for (int i = 0; i < n->data.program.count; i++) {
                assert(n->data.program.functions[i]);
                scopes_build(n->data.program.functions[i], env, NULL);
            }
            break;
        case NODE_FUNCTION: {
            assert(n->data.func.name);
            assert(n->data.func.ret_type);
            SymTab *fscope = (SymTab *)malloc(sizeof *fscope);   /* this function's own scope  */
            assert(fscope);
            symtab_init(fscope, env);                  /* parent = enclosing scope   */
            for (int i = 0; i < n->data.func.param_count; i++) {
                assert(n->data.func.params[i].name);
                assert(n->data.func.params[i].type);
                symtab_add(fscope, n->data.func.params[i].name, n->data.func.params[i].type);
            }
            n->data.func.scope = fscope;               /* dump_scopes reads this back later */
            scopes_build(n->data.func.body, env, fscope);  /* hand the body my scope as `pending` */
            break;
        }
        case NODE_VAR_DECL:
            assert(n->data.var_decl.name);
            assert(n->data.var_decl.type);
            symtab_add(env, n->data.var_decl.name, n->data.var_decl.type);
            break;

        case NODE_BLOCK: {
            SymTab *scope;
            if (pending) {
                scope = pending;                       /* this block IS the function body */
            } else {
                scope = (SymTab *)malloc(sizeof *scope);         /* a genuine nested { } block */
                assert(scope);
                symtab_init(scope, env);               /* parent = enclosing scope */
            }
            n->data.block.scope = scope;               /* dump_scopes reads this back later */
            for (int i = 0; i < n->data.block.count; i++) {
                scopes_build(n->data.block.stmts[i], scope, NULL);  /* deeper blocks are not bodies */
            }
            break;
        }
        case NODE_IF:
            scopes_build(n->data.if_stmt.then_body, env, NULL);
            scopes_build(n->data.if_stmt.else_body, env, NULL);
            break;

        case NODE_WHILE:
            scopes_build(n->data.while_stmt.body, env, NULL);
            break;

        case NODE_FOR:
            scopes_build(n->data.for_stmt.init, env, NULL);
            scopes_build(n->data.for_stmt.body, env, NULL);
            break;

        default:
            break;
    }
}
