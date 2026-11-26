#include <assert.h>
#include <stddef.h>   /* NULL */
#include <string.h>   /* strcmp: load/store get their own judgments */
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "typecheck.h"
#include "types.h"
#include "error.h"
#include "regions.h"

/*
 * The "global" environments don't change as we descend into a function
 * body, so we stash them here at entry instead of threading them through
 * every check_* call. Only the *variable* scope changes per block, so that
 * one stays an explicit parameter.
 */
static SymTab *g_types;   /* unused for now: declared types are already Type* on the AST */
static SymTab *g_funcs;
static SymTab *g_ops;     /* unused until operator overloading */
static std::unordered_map<ASTNode *, TypeScheme> schemes;
/* Scopes are built in advance, but a local becomes visible only after its
   initializer. The environment still retains the original Type* nodes. */
static std::unordered_map<SymTab *, int> visible;

/* Expressions synthesise a type (Γ ⊢ e : τ) and return it; statements just
 * check. A NULL Type means "an error was already reported here" -- callers
 * treat it as poison and must not pile on further errors (no cascades). */
static Type *check_expr(SymTab *scope, ASTNode *e);
static Type *check_expr_inner(SymTab *scope, ASTNode *e);
static void  check_stmt(SymTab *scope, ASTNode *s, Type *ret_type);

/* ---- helpers ---------------------------------------------------------- */

/* Preserve explicit subtype conversions, otherwise solve the equality
 * constraint. A NULL actual is poison from an already-reported error. */
static void expect(Type *actual, Type *expected, int line, const char *what) {
    assert(expected);   /* target may be an unknown, but must be a type node */
    assert(what);
    if (!actual || type_subtype(actual, expected)) return;
    TypeUnifyResult result = type_unify(actual, expected);
    if (result == TYPE_UNIFY_OCCURS)
        error_at(line, "%s: infinite type (%s versus %s)", what,
                 type_name(actual), type_name(expected));
    else if (result != TYPE_UNIFY_OK)
        error_type_mismatch(line, what, expected, actual);
}

static Type *lookup_local(SymTab *scope, const char *name) {
    auto it = visible.find(scope);
    int count = it == visible.end() ? scope->count : it->second;
    for (int i = count - 1; i >= 0; --i)
        if (strcmp(scope->entries[i].name, name) == 0)
            return static_cast<Type *>(scope->entries[i].value);
    return nullptr;
}

static Type *lookup(SymTab *scope, const char *name) {
    for (; scope; scope = scope->parent)
        if (Type *t = lookup_local(scope, name)) return t;
    return nullptr;
}

static Type *pointer_element(Type *pt, int line) {
    if (!pt) return nullptr;
    pt = type_resolve(pt);
    if (pt->kind == TYPE_VAR) {
        Type *elem = type_fresh_var();
        expect(pt, type_ptr_of(elem), line, "argument");
        return elem;
    }
    expect(pt, type_ptr(), line, "argument");
    return pt->kind == TYPE_PTR ? (pt->param ? pt->param : type_int()) : nullptr;
}

static std::vector<Type *> signature(ASTNode *fn) {
    std::vector<Type *> types;
    for (int i = 0; i < fn->data.func.param_count; ++i)
        types.push_back(fn->data.func.params[i].type);
    types.push_back(fn->data.func.ret_type);
    return types;
}

/* ---- expressions:  Γ ⊢ e : τ  ---------------------------------------- */
static Type *check_expr(SymTab *scope, ASTNode *e) {
    if (!e) return nullptr;
    e->inferred_type = check_expr_inner(scope, e);
    return e->inferred_type;
}

static Type *check_expr_inner(SymTab *scope, ASTNode *e) {
    if (!e) return NULL;
    switch (e->kind) {

    /*  ─────────────                                            (axiom)  */
    /*  Γ ⊢ n : int                                                       */
    case NODE_INT_LIT:
        return type_int();
    case NODE_BOOL_LIT:
        return type_bool();

    /*  ─────────────                                            (axiom)  */
    /*  Γ ⊢ 'c' : char                                                    */
    case NODE_CHAR_LIT:
        return type_char();

    /*  Γ(x) = τ                                                          */
    /*  ────────                          (lookup; error if x unbound)    */
    /*  Γ ⊢ x : τ                                                         */
    case NODE_VAR_REF: {
        Type *t = lookup(scope, e->data.var_ref.name);
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
    /* Memory primitives generate pointer constraints directly. Each call */
    /* creates fresh element unknowns; aliases still share their solutions.*/
    /* Explicit bare `ptr` retains its legacy integer load/store behavior. */
    case NODE_CALL: {
        /*  Γ ⊢ n : int                                                    */
        /*  ─────────────────────────  (alloc)                             */
        /*  Γ ⊢ alloc<τ>(n) : ptr<τ>                                       */
        /*                                                                 */
        /* Without <τ>, allocation creates ptr<fresh> and later constraints */
        /* determine the element. Forgetting it through an explicit bare  */
        /* ptr annotation still cannot invent a new element type later.   */
        if (strcmp(e->data.call.name, "alloc") == 0
            && e->data.call.arg_count == 1) {
            Type *nt = check_expr(scope, e->data.call.args[0]);
            expect(nt, type_int(), e->line, "argument");
            return e->data.call.type_arg ? type_ptr_of(e->data.call.type_arg)
                                         : type_ptr_of(type_fresh_var());
        }
        if (strcmp(e->data.call.name, "load") == 0
            && e->data.call.arg_count == 1) {
            return pointer_element(check_expr(scope, e->data.call.args[0]), e->line);
        }
        if (strcmp(e->data.call.name, "store") == 0
            && e->data.call.arg_count == 2) {
            Type *elem = pointer_element(check_expr(scope, e->data.call.args[0]), e->line);
            Type *vt = check_expr(scope, e->data.call.args[1]);
            if (elem) expect(vt, elem, e->line, "argument");
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
            Type *elem = pointer_element(pt, e->line);
            Type *it = check_expr(scope, e->data.call.args[1]);
            expect(it, type_int(), e->line, "argument");
            return elem ? type_resolve(pt) : nullptr;
        }

        ASTNode *fn = (ASTNode *)symtab_lookup(g_funcs, e->data.call.name);
        if (!fn) {
            error_at(e->line, "unknown function '%s'", e->data.call.name);
            return NULL;
        }
        if (strcmp(e->data.call.name, "main") == 0) {
            error_at(e->line, "main cannot be called as a source function");
            return nullptr;
        }
        if (fn->data.func.param_count != e->data.call.arg_count) {
            error_at(e->line, "'%s': expected %d arg(s), got %d",
                     e->data.call.name,
                     fn->data.func.param_count, e->data.call.arg_count);
            return fn->data.func.ret_type;
        }
        auto scheme = schemes.find(fn);
        auto types = scheme == schemes.end() ? signature(fn) : type_instantiate(scheme->second);
        for (int i = 0; i < e->data.call.arg_count; i++) {
            Type *a = check_expr(scope, e->data.call.args[i]);
            expect(a, types[i], e->line, "argument");
        }
        return types.back();
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
        if (lookup_local(scope, s->data.var_decl.name))
            error_at(s->line, "redeclaration of '%s'", s->data.var_decl.name);
        ++visible[scope];
        break;

    /*  x = e :  x must be bound; expect(type(e), type(x)).               */
    case NODE_ASSIGN: {
        Type *lhs = lookup(scope, s->data.assign.name);
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
        SymTab *loop = s->data.for_stmt.scope;
        visible[loop] = 0;
        check_stmt(loop, s->data.for_stmt.init, ret_type);
        Type *c = check_expr(loop, s->data.for_stmt.cond);
        expect(c, type_bool(), s->line, "for condition");
        check_stmt(loop, s->data.for_stmt.body, ret_type);
        check_stmt(loop, s->data.for_stmt.post, ret_type);
        break;
    }

    /*  { ... } :  a block has no type of its own -- just switch to the scope
        build attached to it and check each statement there. (Glue, not a rule.) */
    case NODE_BLOCK:
        assert(s->data.block.scope);   /* build() should have attached it */
        if (s->data.block.scope != scope) visible[s->data.block.scope] = 0;
        for (int i = 0; i < s->data.block.count; i++)
            check_stmt(s->data.block.scope, s->data.block.stmts[i], ret_type);
        break;

    default: (void)check_expr(scope, s); break;
    }
}

/* ---- pass 1: construct (register function signatures) ----------------- */
static void construct(ASTNode *program) {
    /* Store the whole function ASTNode* keyed by name: it already carries the
       return type and param types, so the call rule reads the signature off it.
       Done before pass 2 so calls resolve regardless of definition order. */
    for (int i = 0; i < program->data.program.count; i++) {
        ASTNode *fn = program->data.program.functions[i];
        if (symtab_lookup(g_funcs, fn->data.func.name)) {
            error_at(fn->line, "duplicate or reserved function '%s'", fn->data.func.name);
            continue;
        }
        if (strncmp(fn->data.func.name, "__ce_", 5) == 0) {
            error_at(fn->line, "reserved function name '%s'", fn->data.func.name);
            continue;
        }
        if (g_funcs->count == SYMTAB_MAX) {
            error_at(fn->line, "too many functions");
            continue;
        }
        symtab_add(g_funcs, fn->data.func.name, fn);
        if (strcmp(fn->data.func.name, "main") == 0) {
            if (fn->data.func.param_count != 0)
                error_at(fn->line, "main must have no parameters");
            expect(type_int(), fn->data.func.ret_type, fn->line, "main result");
            if (!type_equals(fn->data.func.ret_type, type_int()))
                error_at(fn->line, "main must return int");
        }
    }
}

static bool returns(ASTNode *n) {
    if (!n) return false;
    if (n->kind == NODE_RETURN) return true;
    if (n->kind == NODE_IF)
        return returns(n->data.if_stmt.then_body) && returns(n->data.if_stmt.else_body);
    if (n->kind == NODE_BLOCK)
        for (int i = 0; i < n->data.block.count; ++i)
            if (returns(n->data.block.stmts[i])) return true;
    return false;
}

/* Tarjan emits dependency groups before their callers. Every edge inside a
   recursive group uses the original monotypes; generalize only once all
   bodies in that group have constrained their shared signatures. */
static void check_functions(ASTNode *program, SymTab *var_env) {
    int count = program->data.program.count;
    ASTNode **functions = program->data.program.functions;
    std::unordered_map<ASTNode *, int> indices;
    for (int i = 0; i < count; ++i) indices[functions[i]] = i;
    std::vector<std::vector<int>> edges(count);
    for (int i = 0; i < count; ++i)
        ast_walk(functions[i]->data.func.body, [&](ASTNode *n) {
            if (n->kind != NODE_CALL) return;
            auto found = indices.find(static_cast<ASTNode *>(symtab_lookup(g_funcs, n->data.call.name)));
            if (found != indices.end()) edges[i].push_back(found->second);
        });

    std::vector<int> number(count, -1), low(count), stack;
    std::vector<bool> active(count, false);
    int next = 0;
    std::function<void(int)> visit = [&](int v) {
        number[v] = low[v] = next++;
        stack.push_back(v); active[v] = true;
        for (int w : edges[v]) {
            if (number[w] == -1) { visit(w); low[v] = std::min(low[v], low[w]); }
            else if (active[w]) low[v] = std::min(low[v], number[w]);
        }
        if (low[v] != number[v]) return;
        std::unordered_set<ASTNode *> group;
        int w;
        do {
            w = stack.back(); stack.pop_back(); active[w] = false;
            group.insert(functions[w]);
        } while (w != v);
        // Source order within a group keeps diagnostics deterministic.
        for (int i = 0; i < count; ++i) {
            ASTNode *fn = functions[i];
            if (!group.count(fn)) continue;
            SymTab *scope = fn->data.func.scope;
            visible[scope] = 0;
            for (int p = 0; p < fn->data.func.param_count; ++p) {
                if (lookup_local(scope, fn->data.func.params[p].name))
                    error_at(fn->line, "duplicate parameter '%s'", fn->data.func.params[p].name);
                ++visible[scope];
            }
            check_stmt(scope, fn->data.func.body, fn->data.func.ret_type);
            // Both emitters supply zero on fallthrough, so it constrains an
            // inferred result too. A pointer result must return on every path.
            if (!returns(fn->data.func.body))
                expect(type_int(), fn->data.func.ret_type, fn->line, "fallthrough result");
        }
        std::vector<Type *> environment;
        for (SymTab *scope = var_env; scope; scope = scope->parent)
            for (int i = 0; i < scope->count; ++i)
                environment.push_back(static_cast<Type *>(scope->entries[i].value));
        for (int i = 0; i < g_funcs->count; ++i) {
            ASTNode *fn = static_cast<ASTNode *>(g_funcs->entries[i].value);
            if (group.count(fn)) continue;
            auto scheme = schemes.find(fn);
            auto vars = type_free_vars(signature(fn));
            for (Type *t : vars)
                if (scheme == schemes.end() ||
                    std::find(scheme->second.quantified.begin(), scheme->second.quantified.end(), t)
                        == scheme->second.quantified.end()) environment.push_back(t);
        }
        for (int i = 0; i < count; ++i)
            if (group.count(functions[i]))
                schemes[functions[i]] = type_generalize(signature(functions[i]), environment);
    };
    for (int i = 0; i < count; ++i) if (number[i] == -1) visit(i);
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
    schemes.clear();
    visible.clear();
    construct(program);                  /* pass 1: signatures */
    if (error_count()) return error_count();
    check_functions(program, var_env);   /* pass 2: bodies     */
    if (!error_count()) regions_analyze(program);
    return error_count();
}

