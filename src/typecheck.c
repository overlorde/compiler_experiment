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
    if (actual && !type_subtype(actual, expected))
        error_at(line, "%s: expected %s, got %s",
                 what, type_name(expected), type_name(actual));
}

/* ---- expressions:  Γ ⊢ e : τ  ---------------------------------------- */
static Type *check_expr(SymTab *scope, ASTNode *e) {
    if (!e) return NULL;
    switch (e->kind) {

    /*  ─────────────                                            (axiom)  */
    /*  Γ ⊢ n : int                                                       */
    case NODE_INT_LIT:
        return type_int();

    /*  Γ(x) = τ                                                          */
    /*  ────────                          (lookup; error if x unbound)    */
    /*  Γ ⊢ x : τ                                                         */
    case NODE_VAR_REF: {
        Type *t = symtab_lookup(scope, e->data.var_ref.name);
        if (!t) {
            error_at(e->line, "unbound variable '%s'", e->data.var_ref.name);
            return NULL;
        }
        return t;
    }

    /*  arithmetic + - * / %  :  int  × int  → int                        */
    /*  comparison < > <= >=  :  int  × int  → bool                       */
    /*  equality   == !=      :  τ    × τ    → bool                       */
    /*  logical    && ||      :  bool × bool → bool                       */
    case NODE_BINARY_OP: {
        Type *l = check_expr(scope, e->data.bin.left);
        Type *r = check_expr(scope, e->data.bin.right);
        switch (e->data.bin.op) {
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
            expect(l, type_int(), e->line, "arithmetic lhs");
            expect(r, type_int(), e->line, "arithmetic rhs");
            return type_int();
        case OP_LT: case OP_GT: case OP_LTE: case OP_GTE:
            expect(l, type_int(), e->line, "comparison lhs");
            expect(r, type_int(), e->line, "comparison rhs");
            return type_bool();
        case OP_EQ: case OP_NEQ:
            if (l && r) expect(l, r, e->line, "equality operands");
            return type_bool();
        case OP_AND: case OP_OR:
            expect(l, type_bool(), e->line, "logical lhs");
            expect(r, type_bool(), e->line, "logical rhs");
            return type_bool();
        }
        return NULL;
    }

    /*  unary  -, ~ : int → int        ! : bool → bool                    */
    case NODE_UNARY_OP: {
        Type *t = check_expr(scope, e->data.unary.operand);
        switch (e->data.unary.op) {
        case OP_NEG: case OP_BITNOT:
            expect(t, type_int(), e->line, "unary operand");
            return type_int();
        case OP_NOT:
            expect(t, type_bool(), e->line, "logical not");
            return type_bool();
        }
        return NULL;
    }

    /*  call: look up callee in g_funcs; check arg count + each arg type   */
    /*        against params[i].type; result type = callee ret_type       */
    case NODE_CALL: {
        ASTNode *fn = symtab_lookup(g_funcs, e->data.call.name);
        if (!fn) {
            error_at(e->line, "unknown function '%s'", e->data.call.name);
            return NULL;
        }
        if (fn->data.func.param_count != e->data.call.arg_count) {
            error_at(e->line, "'%s': expected %d arg(s), got %d",
                     e->data.call.name,
                     fn->data.func.param_count, e->data.call.arg_count);
            return fn->data.func.ret_type;
        }
        for (int i = 0; i < e->data.call.arg_count; i++) {
            Type *a = check_expr(scope, e->data.call.args[i]);
            expect(a, fn->data.func.params[i].type, e->line, "argument");
        }
        return fn->data.func.ret_type;
    }

    default:
        return NULL;   /* not an expression node */
    }
}

/* ---- statements ------------------------------------------------------- */
static void check_stmt(SymTab *scope, ASTNode *s, Type *ret_type) {
    if (!s) return;
    switch (s->kind) {

    /*  T x (= e)? :  if e present, expect(type(e), decl.type).           */
    /*  (Binding already lives in `scope` -- build did symtab_add. Don't   */
    /*   re-bind here, or you'll create a shadowing duplicate.)            */
    case NODE_VAR_DECL:
        if (s->data.var_decl.init) {
            Type *t = check_expr(scope, s->data.var_decl.init);
            expect(t, s->data.var_decl.type, s->line, "initializer");
        }
        break;

    /*  x = e :  x must be bound; expect(type(e), type(x)).               */
    case NODE_ASSIGN: {
        Type *lhs = symtab_lookup(scope, s->data.assign.name);
        if (!lhs) {
            error_at(s->line, "assign to unbound '%s'", s->data.assign.name);
            break;
        }
        Type *rhs = check_expr(scope, s->data.assign.value);
        expect(rhs, lhs, s->line, "assignment");
        break;
    }

    /*  return e :  expect(type(e), ret_type).                            */
    case NODE_RETURN:
        if (s->data.ret.expr) {
            Type *t = check_expr(scope, s->data.ret.expr);
            expect(t, ret_type, s->line, "return value");
        }
        break;

    /*  e ; :  just type-check e for well-formedness.                     */
    case NODE_EXPR_STMT:
        (void)check_expr(scope, s->data.expr_stmt.expr);
        break;

    /*  if/while/for :  condition must be bool; check the body/branches.  */
    case NODE_IF: {
        Type *c = check_expr(scope, s->data.if_stmt.cond);
        expect(c, type_bool(), s->line, "if condition");
        check_stmt(scope, s->data.if_stmt.then_body, ret_type);
        check_stmt(scope, s->data.if_stmt.else_body, ret_type);
        break;
    }
    case NODE_WHILE: {
        Type *c = check_expr(scope, s->data.while_stmt.cond);
        expect(c, type_bool(), s->line, "while condition");
        check_stmt(scope, s->data.while_stmt.body, ret_type);
        break;
    }
    case NODE_FOR: {
        check_stmt(scope, s->data.for_stmt.init, ret_type);
        Type *c = check_expr(scope, s->data.for_stmt.cond);
        expect(c, type_bool(), s->line, "for condition");
        check_stmt(scope, s->data.for_stmt.post, ret_type);
        check_stmt(scope, s->data.for_stmt.body, ret_type);
        break;
    }

    /*  { ... } :  a block has no type of its own -- just switch to the scope
        build attached to it and check each statement there. (Glue, not a rule.) */
    case NODE_BLOCK:
        for (int i = 0; i < s->data.block.count; i++)
            check_stmt(s->data.block.scope, s->data.block.stmts[i], ret_type);
        break;

    default: break;
    }
}

/* ---- pass 1: construct (register function signatures) ----------------- */
static void construct(ASTNode *program) {
    /* Store the whole function ASTNode* keyed by name: it already carries the
       return type and param types, so the call rule reads the signature off it.
       Done before pass 2 so calls resolve regardless of definition order. */
    for (int i = 0; i < program->data.program.count; i++) {
        ASTNode *fn = program->data.program.functions[i];
        symtab_add(g_funcs, fn->data.func.name, fn);
    }
}

/* ---- pass 2: check each function body --------------------------------- */
static void check_functions(ASTNode *program, SymTab *var_env) {
    /* `build` already constructed each function's scope (params + body locals)
       and chained it off var_env, stashing it on fn->data.func.scope. So pass 2
       just reads that scope and checks the body against the declared return type
       -- no scope (re)construction happens in the checker. */
    (void)var_env;   /* scopes were chained to it at build time */
    for (int i = 0; i < program->data.program.count; i++) {
        ASTNode *fn = program->data.program.functions[i];
        check_stmt(fn->data.func.scope, fn->data.func.body, fn->data.func.ret_type);
    }
}

/* ---- built-in (extern) function signatures ---------------------------- */
/* These look like ordinary user-function ASTNodes so the existing call rule
 * type-checks them with no special case. body == NULL means "no body in this
 * translation unit"; the real implementation lives in runtime/runtime.c and
 * is resolved by the linker. Codegen will declare matching LLVM externs.
 *
 * Storage is file-scope static, never freed -- they outlive any compilation,
 * and ast_free never reaches them (it walks the program AST, not func_env). */

static char  bi_arg_name[] = "x";   /* shared param name; mutable to satisfy Param.name : char* */

static Param print_int_params [1];
static Param print_bool_params[1];

static ASTNode print_int_sig  = { .kind = NODE_FUNCTION };
static ASTNode print_bool_sig = { .kind = NODE_FUNCTION };

void typecheck_register_builtins(SymTab *func_env) {
    /* type_int()/type_bool() aren't constant expressions, so the param types
     * and return types get filled at runtime, once, on first call. */
    print_int_params[0]  = (Param){ .name = bi_arg_name, .type = type_int()  };
    print_bool_params[0] = (Param){ .name = bi_arg_name, .type = type_bool() };

    print_int_sig.data.func = (typeof(print_int_sig.data.func)){
        .ret_type    = type_int(),
        .name        = "print_int",
        .params      = print_int_params,
        .param_count = 1,
        .body        = NULL,
        .scope       = NULL,
    };
    print_bool_sig.data.func = (typeof(print_bool_sig.data.func)){
        .ret_type    = type_int(),   /* bool lowers to i32; runtime returns int */
        .name        = "print_bool",
        .params      = print_bool_params,
        .param_count = 1,
        .body        = NULL,
        .scope       = NULL,
    };

    symtab_add(func_env, "print_int",  &print_int_sig);
    symtab_add(func_env, "print_bool", &print_bool_sig);
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

