#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "symtab.h"
#include "types.h"
#include "typecheck.h"
#include "env_print.h"


static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "error: cannot open '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}


/* `pending` carries a function's scope down to its body block: non-NULL means
 * "the next block you hit IS this function's body -- put its locals here instead
 * of opening a new scope". It's consumed once (cleared to NULL for deeper calls),
 * so only the function body reuses it; every other block is genuinely nested. */
static void build(ASTNode *n, SymTab *env, SymTab *pending){

    if(!n ){
        return;
    }

    switch (n->kind) {
        case NODE_PROGRAM:
            for(int i=0; i< n->data.program.count; i++){
                build(n->data.program.functions[i], env, NULL);
            }
            break;
        case  NODE_FUNCTION: {
            SymTab *fscope = malloc(sizeof *fscope);   /* this function's own scope  */
            symtab_init(fscope, env);                  /* parent = enclosing scope   */
            for (int i = 0; i < n->data.func.param_count; i++ ){
                symtab_add(fscope, n->data.func.params[i].name, n->data.func.params[i].type);
            }
            n->data.func.scope = fscope;               /* dump_scopes reads this back later */
            build(n->data.func.body, env, fscope);     /* hand the body my scope as `pending` */
            break;
        }
        case NODE_VAR_DECL:
            symtab_add(env, n->data.var_decl.name, n->data.var_decl.type);
            break;

        case NODE_BLOCK:{
            SymTab *scope;
            if (pending) {
                scope = pending;                       /* this block IS the function body */
            } else {
                scope = malloc(sizeof *scope);         /* a genuine nested { } block */
                symtab_init(scope, env);               /* parent = enclosing scope */
            }
            n->data.block.scope = scope;               /* dump_scopes reads this back later */
            for (int i = 0; i < n->data.block.count; i++) {
                build(n->data.block.stmts[i], scope, NULL);  /* deeper blocks are not bodies */
            }
            break;
        }
        case NODE_IF:
            build(n->data.if_stmt.then_body, env, NULL);
            build(n->data.if_stmt.else_body, env, NULL);
            break;

        case NODE_WHILE:
            build(n->data.while_stmt.body, env, NULL);
            break;

        case NODE_FOR:
            build(n->data.for_stmt.init, env, NULL);
            build(n->data.for_stmt.body, env, NULL);
        break;

        default:
            break;
        }
}


/* Print the scope tree build just wired up: each scope EXACTLY once, top-down,
 * indented to its depth (env_dump derives the indent from the scope itself). It
 * reads the SymTab* that build stashed on each function/block node -- so this is
 * the first real "query later" reader, and a check that the annotation is sound.
 *
 * Only structural nodes carry a scope; we recurse just far enough to reach the
 * blocks nested inside them. A function body is the folded function scope (the
 * `pending` trick), so we print it once at the function and then step straight
 * into its statements rather than reprinting it as a block. */
static void dump_scopes(ASTNode *n) {
    if (!n) return;
    switch (n->kind) {
        case NODE_PROGRAM:
            for (int i = 0; i < n->data.program.count; i++)
                dump_scopes(n->data.program.functions[i]);
            break;
        case NODE_FUNCTION:
            env_dump(stdout, n->data.func.name, n->data.func.scope, render_type);
            if (n->data.func.body && n->data.func.body->kind == NODE_BLOCK) {
                ASTNode *body = n->data.func.body;             /* already printed above */
                for (int i = 0; i < body->data.block.count; i++)
                    dump_scopes(body->data.block.stmts[i]);
            } else {
                dump_scopes(n->data.func.body);
            }
            break;
        case NODE_BLOCK:
            env_dump(stdout, "block", n->data.block.scope, render_type);
            for (int i = 0; i < n->data.block.count; i++)
                dump_scopes(n->data.block.stmts[i]);
            break;
        case NODE_IF:
            dump_scopes(n->data.if_stmt.then_body);
            dump_scopes(n->data.if_stmt.else_body);
            break;
        case NODE_WHILE:
            dump_scopes(n->data.while_stmt.body);
            break;
        case NODE_FOR:
            dump_scopes(n->data.for_stmt.init);
            dump_scopes(n->data.for_stmt.body);
            break;
        default:
            break;
    }
}


int main(int argc, char **argv) {
    CodegenOptions opts = {0, 0};
    int i;

    /* Parse flags */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') break;
        if (strcmp(argv[i], "-emit-llvm") == 0)
            opts.emit_llvm = 1;
        else if (strcmp(argv[i], "-O1") == 0)
            opts.opt_level = 1;
        else {
            fprintf(stderr, "error: unknown flag '%s'\n", argv[i]);
            return 1;
        }
    }

    if (argc - i != 2) {
        fprintf(stderr, "Usage: %s [flags] <input.c> <output>\n", argv[0]);
        fprintf(stderr, "Flags: -emit-llvm  -O1\n");
        return 1;
    }

    const char *input_path  = argv[i];
    const char *output_path = argv[i + 1];

    /* Read source file */
    char *source = read_file(input_path);
    if (!source) return 1;

    /* Parse */
    Parser parser;
    parser_init(&parser, source);
    ASTNode *ast = parser_parse(&parser);


    //  ASTNode *ast = typecheck()
    if (!ast) {
        fprintf(stderr, "error: parsing failed\n");
        free(source);
        return 1;
    }

    /* Environments for the type checker (the upcoming typecheck pass will consume these):
         type_env: type names -> Type       (seeded with the built-in primitives)
         var_env:  variable names -> Binding (root scope; per-function/block scopes chain off it)
         op_env:   operator -> overload set  (for operator overloading; empty until we define
                                              an operator-signature representation) */
    SymTab type_env, var_env, op_env, func_env;

    symtab_init(&type_env, NULL);
    symtab_init(&var_env,  NULL);
    symtab_init(&op_env,   NULL);
    symtab_init(&func_env,   NULL);

    symtab_add(&type_env, "int",  type_int());
    symtab_add(&type_env, "bool", type_bool());

    build(ast, &var_env, NULL);

    /* Inspect what build produced. dump_scopes walks the scope TREE top-down (one
       line per scope, indented by depth); the two env_dumps below show the two
       flat root environments. Each SymTab stores values as void*, so env_dump
       takes a renderer that knows the value type -- here all hold Type*. */
    dump_scopes(ast);
    env_dump(stdout, "type_env", &type_env, render_type);
    env_dump(stdout, "var_env",  &var_env,  render_type);

    int error_code = typecheck(ast, &type_env, &func_env, &var_env, &op_env);

    // error code
    if(error_code){
        fprintf(stderr, "error: typecheck failed\n");
        free(source);
        return 1;
    }


    /* Codegen */
    int result = codegen(ast, output_path, &opts);

    /* Cleanup */
    ast_free(ast);
    free(source);

    return result;
}

