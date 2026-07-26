#include <assert.h>
#include <stddef.h>   /* NULL */
#include <string.h>   /* strcmp: load/store get their own judgments */

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
    assert(expected);   /* every premise has a known target type -- NULL is a checker bug */
    assert(what);
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

    /*  ─────────────                                            (axiom)  */
    /*  Γ ⊢ 'c' : char                                                    */
    case NODE_CHAR_LIT:
        return type_char();

    /*  Γ(x) = τ                                                          */
    /*  ────────                          (lookup; error if x unbound)    */
    /*  Γ ⊢ x : τ                                                         */
    case NODE_VAR_REF: {
        Type *t = (Type *)symtab_lookup(scope, e->data.var_ref.name);
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

    /*  Γ ⊢ p : ptr<τ>                Γ ⊢ p : ptr<τ>   Γ ⊢ v : τ          */
    /*  ────────────────  (load)      ─────────────────────────  (store)  */
    /*  Γ ⊢ load(p) : τ               Γ ⊢ store(p, v) : int               */
    /*                                                                     */
    /*  The generic call rule below can't express a result type that       */
    /*  depends on the argument's phantom parameter, so these two          */
    /*  primitives get their own judgments (until real polymorphism --     */
    /*  HM, phase 3 -- subsumes them). Bare `ptr` reads/writes int, which  */
    /*  keeps pre-phantom programs checking unchanged.                     */
    case NODE_CALL: {
        /*  Γ ⊢ n : int                                                    */
        /*  ─────────────────────────  (alloc)                             */
        /*  Γ ⊢ alloc<τ>(n) : ptr<τ>                                       */
        /*                                                                 */
        /*  The element type is an explicit argument because nothing here  */
        /*  can infer it (that's HM's job later). Without <τ> the result   */
        /*  is bare ptr -- usable with the int-typed load/store, but not   */
        /*  convertible to any ptr<T>: subtyping only forgets pointees,    */
        /*  never invents them.                                            */
        if (strcmp(e->data.call.name, "alloc") == 0
            && e->data.call.arg_count == 1) {
            Type *nt = check_expr(scope, e->data.call.args[0]);
            expect(nt, type_int(), e->line, "argument");
            return e->data.call.type_arg ? type_ptr_of(e->data.call.type_arg)
                                         : type_ptr();
        }
        if (strcmp(e->data.call.name, "load") == 0
            && e->data.call.arg_count == 1) {
            Type *pt = check_expr(scope, e->data.call.args[0]);
            expect(pt, type_ptr(), e->line, "argument");
            if (pt && pt->kind == TYPE_PTR && pt->param)
                return pt->param;
            return type_int();
        }
        if (strcmp(e->data.call.name, "store") == 0
            && e->data.call.arg_count == 2) {
            Type *pt = check_expr(scope, e->data.call.args[0]);
            expect(pt, type_ptr(), e->line, "argument");
            Type *elem = (pt && pt->kind == TYPE_PTR && pt->param)
                       ? pt->param : type_int();
            Type *vt = check_expr(scope, e->data.call.args[1]);
            expect(vt, elem, e->line, "argument");
            return type_int();
        }

        /*  Γ ⊢ p : ptr<τ>   Γ ⊢ i : int                                   */
        /*  ─────────────────────────────  (offset)                        */
        /*  Γ ⊢ offset(p, i) : ptr<τ>                                      */
        /*                                                                 */
        /*  The phantom survives arithmetic: cell i of an int block is     */
        /*  still an int slot. Note what the rule does NOT check: that i   */
        /*  is in bounds. Lengths are runtime values, and Γ doesn't speak  */
        /*  about values -- out-of-range offsets typecheck and crash, by   */
        /*  explicit design choice (the C road).                           */
        if (strcmp(e->data.call.name, "offset") == 0
            && e->data.call.arg_count == 2) {
            Type *pt = check_expr(scope, e->data.call.args[0]);
            expect(pt, type_ptr(), e->line, "argument");
            Type *it = check_expr(scope, e->data.call.args[1]);
            expect(it, type_int(), e->line, "argument");
            return (pt && pt->kind == TYPE_PTR) ? pt : type_ptr();
        }

        ASTNode *fn = (ASTNode *)symtab_lookup(g_funcs, e->data.call.name);
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
        Type *lhs = (Type *)symtab_lookup(scope, s->data.assign.name);
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
        assert(s->data.block.scope);   /* build() should have attached it */
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
        assert(fn);
        assert(fn->kind == NODE_FUNCTION);
        assert(fn->data.func.scope);     /* build() invariant */
        assert(fn->data.func.ret_type);  /* parser invariant */
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
static Param print_char_params[1];
static Param alloc_params     [1];
static Param load_params      [1];
static Param store_params     [2];
static Param offset_params    [2];

static ASTNode print_int_sig;
static ASTNode print_bool_sig;
static ASTNode print_char_sig;
static ASTNode alloc_sig;
static ASTNode load_sig;
static ASTNode store_sig;
static ASTNode offset_sig;

/* Helper: fill an ASTNode in-place as a builtin function signature.
 * Replaces the C99 compound-literal pattern that C++ doesn't accept. */
static void make_builtin_sig(ASTNode *sig, const char *name, Type *ret,
                             Param *params, int nparams) {
    sig->kind = NODE_FUNCTION;
    sig->data.func.ret_type    = ret;
    sig->data.func.name        = (char *)name;
    sig->data.func.params      = params;
    sig->data.func.param_count = nparams;
    sig->data.func.body        = NULL;
    sig->data.func.scope       = NULL;
}

void typecheck_register_builtins(SymTab *func_env) {
    /* type_int()/type_bool() aren't constant expressions, so the param types
     * and return types get filled at runtime, once, on first call. */
    print_int_params[0].name  = bi_arg_name; print_int_params[0].type  = type_int();
    print_bool_params[0].name = bi_arg_name; print_bool_params[0].type = type_bool();
    print_char_params[0].name = bi_arg_name; print_char_params[0].type = type_char();

    /* Heap primitives. ptr is nominal: no subtype edges connect it to the
     * numeric types, so load(42) or alloc(8)+1 die in the ordinary call and
     * operator rules -- the signatures below are the entire safety story. */
    alloc_params[0].name = bi_arg_name; alloc_params[0].type = type_int();
    load_params [0].name = bi_arg_name; load_params [0].type = type_ptr();
    store_params[0].name = bi_arg_name; store_params[0].type = type_ptr();
    store_params[1].name = bi_arg_name; store_params[1].type = type_int();
    offset_params[0].name = bi_arg_name; offset_params[0].type = type_ptr();
    offset_params[1].name = bi_arg_name; offset_params[1].type = type_int();

    make_builtin_sig(&print_int_sig,  "print_int",  type_int(), print_int_params,  1);
    make_builtin_sig(&print_bool_sig, "print_bool", type_int(), print_bool_params, 1);
    make_builtin_sig(&print_char_sig, "print_char", type_int(), print_char_params, 1);
    make_builtin_sig(&alloc_sig,      "alloc",      type_ptr(), alloc_params,      1);
    make_builtin_sig(&load_sig,       "load",       type_int(), load_params,       1);
    make_builtin_sig(&store_sig,      "store",      type_int(), store_params,      2);
    make_builtin_sig(&offset_sig,     "offset",     type_ptr(), offset_params,     2);

    symtab_add(func_env, "print_int",  &print_int_sig);
    symtab_add(func_env, "print_bool", &print_bool_sig);
    symtab_add(func_env, "print_char", &print_char_sig);
    symtab_add(func_env, "alloc",      &alloc_sig);
    symtab_add(func_env, "load",       &load_sig);
    symtab_add(func_env, "store",      &store_sig);
    symtab_add(func_env, "offset",     &offset_sig);
}

/* ---- entry ------------------------------------------------------------ */
int typecheck(ASTNode *program, SymTab *type_env, SymTab *func_env,
              SymTab *var_env, SymTab *op_env) {
    assert(program);
    assert(program->kind == NODE_PROGRAM);
    assert(type_env && func_env && var_env && op_env);
    g_types = type_env;
    g_funcs = func_env;
    g_ops   = op_env;

    error_reset();
    construct(program);                  /* pass 1: signatures */
    check_functions(program, var_env);   /* pass 2: bodies     */
    return error_count();
}

