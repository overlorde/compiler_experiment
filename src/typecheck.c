#include <stddef.h>   /* NULL */

#include "typecheck.h"
#include "types.h"
#include "error.h"

/*
 * The "global" environments don't change as we descend into a function
 * body, so we stash them here at entry instead of threading them through
 * every check_* call. Only the *variable* scope changes per block, so that
 * one stays an explicit parameter.
 */
static SymTab *g_types;   /* unused for now: declared types are already Type* on the AST */
static SymTab *g_funcs;
static SymTab *g_ops;     /* unused until operator overloading */

/* Expressions synthesise a type (Γ ⊢ e : τ) and return it; statements just
 * check. A NULL Type means "an error was already reported here" -- callers
 * treat it as poison and must not pile on further errors (no cascades). */
static Type *check_expr(SymTab *scope, ASTNode *e);
static void  check_stmt(SymTab *scope, ASTNode *s, Type *ret_type);

/* ---- helpers ---------------------------------------------------------- */

/* Premise check: require `actual` to equal `expected`. A NULL `actual` means
 * a prior error already fired, so stay quiet. */
static void expect(Type *actual, Type *expected, int line, const char *what) {
    /* TODO:
       if (actual && !type_equals(actual, expected))
           error_at(line, "%s: expected %s, got %s",
                    what, type_name(expected), type_name(actual)); */
    (void)actual; (void)expected; (void)line; (void)what;
}

/* ---- expressions:  Γ ⊢ e : τ  ---------------------------------------- */
static Type *check_expr(SymTab *scope, ASTNode *e) {
    if (!e) return NULL;
    switch (e->kind) {

    /*  ─────────────                                            (axiom)  */
    /*  Γ ⊢ n : int                                                       */
    case NODE_INT_LIT:
        return NULL;   /* TODO: return type_int(); */

    /*  Γ(x) = τ                                                          */
    /*  ────────                          (lookup; error if x unbound)    */
    /*  Γ ⊢ x : τ                                                         */
    case NODE_VAR_REF:
        return NULL;   /* TODO: t = symtab_lookup(scope, name);
                                if (!t) error_at(e->line, ...); return t; */

    /*  arithmetic + - * / %  :  int  × int  → int                        */
    /*  comparison < > <= >=  :  int  × int  → bool                       */
    /*  equality   == !=      :  τ    × τ    → bool                       */
    /*  logical    && ||      :  bool × bool → bool                       */
    case NODE_BINARY_OP:
        return NULL;   /* TODO: l = check_expr(scope, e->data.bin.left);
                                r = check_expr(scope, e->data.bin.right);
                                expect(...) per op; return the result type */

    /*  unary  -, ~ : int → int        ! : bool → bool                    */
    case NODE_UNARY_OP:
        return NULL;   /* TODO */

    /*  call: look up callee in g_funcs; check arg count + each arg type   */
    /*        against params[i].type; result type = callee ret_type       */
    case NODE_CALL:
        return NULL;   /* TODO */

    default:
        return NULL;   /* not an expression node */
    }
}

/* ---- statements ------------------------------------------------------- */
static void check_stmt(SymTab *scope, ASTNode *s, Type *ret_type) {
    if (!s) return;
    switch (s->kind) {

    /*  T x (= e)? :  if e present, expect(type(e), decl.type);           */
    /*                then bind x -> decl.type   (reject redeclaration).   */
    case NODE_VAR_DECL:    /* TODO */ break;

    /*  x = e :  x must be bound; expect(type(e), type(x)).               */
    case NODE_ASSIGN:      /* TODO */ break;

    /*  return e :  expect(type(e), ret_type).                            */
    case NODE_RETURN:      /* TODO */ break;

    /*  e ; :  just type-check e for well-formedness.                     */
    case NODE_EXPR_STMT:   /* TODO */ break;

    /*  if/while/for :  condition must be bool; check the body/branches.  */
    case NODE_IF:          /* TODO */ break;
    case NODE_WHILE:       /* TODO */ break;
    case NODE_FOR:         /* TODO: child scope for an `int i = ...` init  */ break;

    /*  { ... } :  open a child scope, check each statement in it.        */
    case NODE_BLOCK:       /* TODO */ break;

    default: break;
    }
    (void)scope; (void)ret_type;
}

/* ---- pass 1: construct (register function signatures) ----------------- */
static void construct(ASTNode *program) {
    /* TODO: for each fn in program->data.program.functions:
                symtab_add(g_funcs, fn->data.func.name, fn);
       (store the ASTNode* itself -- it already carries ret_type + param types) */
    (void)program;
}

/* ---- pass 2: check each function body --------------------------------- */
static void check_functions(ASTNode *program, SymTab *var_env) {
    /* TODO: for each fn:
                SymTab scope; symtab_init(&scope, var_env);          // function scope
                for each param: symtab_add(&scope, params[i].name, <its type>);
                check_stmt(&scope, fn->data.func.body, fn->data.func.ret_type); */
    (void)program; (void)var_env;
}

/* ---- entry ------------------------------------------------------------ */
int typecheck(ASTNode *program, SymTab *type_env, SymTab *func_env,
              SymTab *var_env, SymTab *op_env) {
    g_types = type_env;
    g_funcs = func_env;
    g_ops   = op_env;

    error_reset();
    construct(program);                  /* pass 1: signatures */
    check_functions(program, var_env);   /* pass 2: bodies     */
    return error_count();
}
