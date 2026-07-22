// irprint.cpp -- AST -> LLVM IR as text, no LLVM linked.
//
// Mirrors codegen.cpp case for case: where codegen calls
// b.CreateAdd(l, r, "add"), this prints "%.tN = add i32 L, R". The state the
// IRBuilder tracked for us -- fresh value names, label names, which register
// holds which variable's slot, whether the current block is terminated -- is
// carried explicitly in Printer.
//
// Naming scheme: temporaries are %.t1, %.t2, ... and allocas are
// %<var>.addr.<K>. Both contain a dot, which source identifiers can't,
// so user names (function parameters appear as %<name>) never collide.
// Named values also sidestep LLVM's strict sequential-numbering rules for
// numeric %N names.

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "irprint.h"

namespace {

struct Slot { std::string name; std::string addr; };

struct Printer {
    std::string out;
    std::vector<Slot> syms;
    int tmp = 0;     // %.tN counter, per function
    int addr = 0;    // %x.addr.K counter, per function
    int label = 0;   // suffix for then/else/... labels, per function
    bool terminated = false;  // current block already ended in ret/br?
};

static std::string sym_lookup(const Printer &p, const char *name) {
    for (auto it = p.syms.rbegin(); it != p.syms.rend(); ++it)
        if (it->name == name) return it->addr;
    assert(false && "irprint: name unbound after typecheck");
    return "";
}

static std::string fresh_tmp(Printer &p) {
    return "%.t" + std::to_string(++p.tmp);
}

// Append one instruction line. If the current block is already terminated,
// codegen would be emitting into a dead block (invalid IR); we open a fresh
// unreachable label instead so the output always assembles.
static void inst(Printer &p, const std::string &line) {
    if (p.terminated) {
        p.out += "dead." + std::to_string(++p.label) + ":\n";
        p.terminated = false;
    }
    p.out += "  " + line + "\n";
}

static void label(Printer &p, const std::string &name) {
    p.out += name + ":\n";
    p.terminated = false;
}

// Terminators go through inst() too (a dead ret after a ret still needs a
// label to be valid), then mark the block closed.
static void terminator(Printer &p, const std::string &line) {
    inst(p, line);
    p.terminated = true;
}

static std::string emit_expr(Printer &p, ASTNode *node);

static std::string emit_unary(Printer &p, UnaryOp op, const std::string &v) {
    std::string t = fresh_tmp(p);
    switch (op) {
    case OP_NEG:
        inst(p, t + " = sub i32 0, " + v);
        return t;
    case OP_NOT: {
        // !x  ==  (x == 0); widen the i1 result back to i32.
        inst(p, t + " = icmp eq i32 " + v + ", 0");
        std::string e = fresh_tmp(p);
        inst(p, e + " = zext i1 " + t + " to i32");
        return e;
    }
    case OP_BITNOT:
        inst(p, t + " = xor i32 " + v + ", -1");
        return t;
    }
    assert(false && "emit_unary: unknown UnaryOp");
    return "";
}

static std::string emit_binop(Printer &p, BinaryOp op,
                              const std::string &l, const std::string &r) {
    const char *arith = nullptr;
    const char *cmp = nullptr;
    switch (op) {
    case OP_ADD: arith = "add";  break;
    case OP_SUB: arith = "sub";  break;
    case OP_MUL: arith = "mul";  break;
    case OP_DIV: arith = "sdiv"; break;
    case OP_MOD: arith = "srem"; break;
    case OP_LT:  cmp = "slt"; break;
    case OP_GT:  cmp = "sgt"; break;
    case OP_LTE: cmp = "sle"; break;
    case OP_GTE: cmp = "sge"; break;
    case OP_EQ:  cmp = "eq";  break;
    case OP_NEQ: cmp = "ne";  break;
    case OP_AND: case OP_OR: {
        // Bitwise AND/OR of (l != 0) and (r != 0), widened back. Not
        // short-circuit -- both sides are already evaluated, same as codegen.
        std::string lb = fresh_tmp(p);
        inst(p, lb + " = icmp ne i32 " + l + ", 0");
        std::string rb = fresh_tmp(p);
        inst(p, rb + " = icmp ne i32 " + r + ", 0");
        std::string res = fresh_tmp(p);
        inst(p, res + " = " + (op == OP_AND ? std::string("and") : std::string("or"))
                + " i1 " + lb + ", " + rb);
        std::string e = fresh_tmp(p);
        inst(p, e + " = zext i1 " + res + " to i32");
        return e;
    }
    }
    std::string t = fresh_tmp(p);
    if (arith) {
        inst(p, t + " = " + arith + " i32 " + l + ", " + r);
        return t;
    }
    assert(cmp);
    inst(p, t + " = icmp " + std::string(cmp) + " i32 " + l + ", " + r);
    std::string e = fresh_tmp(p);
    inst(p, e + " = zext i1 " + t + " to i32");
    return e;
}

static std::string emit_expr(Printer &p, ASTNode *node) {
    assert(node);
    switch (node->kind) {
    case NODE_INT_LIT:
        return std::to_string(node->data.int_lit.value);
    case NODE_CHAR_LIT:
        return std::to_string(node->data.char_lit.value);

    case NODE_UNARY_OP: {
        std::string v = emit_expr(p, node->data.unary.operand);
        return emit_unary(p, node->data.unary.op, v);
    }

    case NODE_BINARY_OP: {
        std::string l = emit_expr(p, node->data.bin.left);
        std::string r = emit_expr(p, node->data.bin.right);
        return emit_binop(p, node->data.bin.op, l, r);
    }

    case NODE_VAR_REF: {
        std::string a = sym_lookup(p, node->data.var_ref.name);
        std::string t = fresh_tmp(p);
        inst(p, t + " = load i32, ptr " + a + ", align 4");
        return t;
    }

    case NODE_CALL: {
        std::string args;
        for (int i = 0; i < node->data.call.arg_count; i++) {
            if (i) args += ", ";
            args += "i32 " + emit_expr(p, node->data.call.args[i]);
        }
        std::string t = fresh_tmp(p);
        inst(p, t + " = call i32 @" + node->data.call.name + "(" + args + ")");
        return t;
    }

    default:
        assert(false && "emit_expr: unhandled NodeKind in expression position");
        return "";
    }
}

// Build an i1 from an i32 condition (cond != 0).
static std::string cond_to_i1(Printer &p, const std::string &v) {
    std::string t = fresh_tmp(p);
    inst(p, t + " = icmp ne i32 " + v + ", 0");
    return t;
}

static std::string emit_alloca(Printer &p, const char *name) {
    std::string a = "%" + std::string(name) + ".addr." + std::to_string(p.addr++);
    inst(p, a + " = alloca i32, align 4");
    p.syms.push_back({name, a});
    return a;
}

static void emit_stmt(Printer &p, ASTNode *node);

static void emit_block(Printer &p, ASTNode *node) {
    // Shadowing: anything declared inside falls out of scope at block exit.
    size_t saved = p.syms.size();
    for (int i = 0; i < node->data.block.count; i++)
        emit_stmt(p, node->data.block.stmts[i]);
    p.syms.resize(saved);
}

static void emit_stmt(Printer &p, ASTNode *node) {
    assert(node);
    switch (node->kind) {
    case NODE_RETURN: {
        std::string v = emit_expr(p, node->data.ret.expr);
        terminator(p, "ret i32 " + v);
        return;
    }

    case NODE_VAR_DECL: {
        std::string a = emit_alloca(p, node->data.var_decl.name);
        if (node->data.var_decl.init) {
            std::string v = emit_expr(p, node->data.var_decl.init);
            inst(p, "store i32 " + v + ", ptr " + a + ", align 4");
        }
        return;
    }

    case NODE_ASSIGN: {
        std::string a = sym_lookup(p, node->data.assign.name);
        std::string v = emit_expr(p, node->data.assign.value);
        inst(p, "store i32 " + v + ", ptr " + a + ", align 4");
        return;
    }

    case NODE_EXPR_STMT:
        emit_expr(p, node->data.expr_stmt.expr);
        return;

    case NODE_BLOCK:
        emit_block(p, node);
        return;

    case NODE_IF: {
        std::string c = emit_expr(p, node->data.if_stmt.cond);
        std::string cmp = cond_to_i1(p, c);

        int n = ++p.label;
        std::string then_l  = "then."    + std::to_string(n);
        std::string else_l  = "else."    + std::to_string(n);
        std::string merge_l = "ifmerge." + std::to_string(n);

        terminator(p, "br i1 " + cmp + ", label %" + then_l + ", label %" + else_l);

        label(p, then_l);
        emit_stmt(p, node->data.if_stmt.then_body);
        if (!p.terminated) terminator(p, "br label %" + merge_l);

        label(p, else_l);
        if (node->data.if_stmt.else_body)
            emit_stmt(p, node->data.if_stmt.else_body);
        if (!p.terminated) terminator(p, "br label %" + merge_l);

        label(p, merge_l);
        return;
    }

    case NODE_WHILE: {
        int n = ++p.label;
        std::string cond_l  = "whilecond."  + std::to_string(n);
        std::string body_l  = "whilebody."  + std::to_string(n);
        std::string after_l = "whileafter." + std::to_string(n);

        terminator(p, "br label %" + cond_l);

        label(p, cond_l);
        std::string c = emit_expr(p, node->data.while_stmt.cond);
        std::string cmp = cond_to_i1(p, c);
        terminator(p, "br i1 " + cmp + ", label %" + body_l + ", label %" + after_l);

        label(p, body_l);
        emit_stmt(p, node->data.while_stmt.body);
        if (!p.terminated) terminator(p, "br label %" + cond_l);

        label(p, after_l);
        return;
    }

    case NODE_FOR: {
        size_t saved = p.syms.size();

        if (node->data.for_stmt.init)
            emit_stmt(p, node->data.for_stmt.init);

        int n = ++p.label;
        std::string cond_l  = "forcond."  + std::to_string(n);
        std::string body_l  = "forbody."  + std::to_string(n);
        std::string post_l  = "forpost."  + std::to_string(n);
        std::string after_l = "forafter." + std::to_string(n);

        terminator(p, "br label %" + cond_l);

        label(p, cond_l);
        if (node->data.for_stmt.cond) {
            std::string c = emit_expr(p, node->data.for_stmt.cond);
            std::string cmp = cond_to_i1(p, c);
            terminator(p, "br i1 " + cmp + ", label %" + body_l + ", label %" + after_l);
        } else {
            terminator(p, "br label %" + body_l);
        }

        label(p, body_l);
        emit_stmt(p, node->data.for_stmt.body);
        if (!p.terminated) terminator(p, "br label %" + post_l);

        label(p, post_l);
        if (node->data.for_stmt.post)
            emit_stmt(p, node->data.for_stmt.post);
        terminator(p, "br label %" + cond_l);

        label(p, after_l);
        p.syms.resize(saved);   /* for-init declarations scope to the loop */
        return;
    }

    default:
        assert(false && "emit_stmt: unhandled NodeKind in statement position");
    }
}

static void emit_function(Printer &p, ASTNode *fn) {
    assert(fn && fn->kind == NODE_FUNCTION);
    p.tmp = 0;
    p.addr = 0;
    p.label = 0;
    p.syms.clear();

    p.out += "define i32 @" + std::string(fn->data.func.name) + "(";
    for (int j = 0; j < fn->data.func.param_count; j++) {
        if (j) p.out += ", ";
        p.out += "i32 %" + std::string(fn->data.func.params[j].name);
    }
    p.out += ") {\n";
    label(p, "entry");

    // Alloca + store each parameter so it lives in a slot the body can
    // load/store, same shape as locals.
    for (int j = 0; j < fn->data.func.param_count; j++) {
        const char *name = fn->data.func.params[j].name;
        std::string a = emit_alloca(p, name);
        inst(p, "store i32 %" + std::string(name) + ", ptr " + a + ", align 4");
    }

    emit_stmt(p, fn->data.func.body);

    // Control flow can fall off the end (e.g. an empty ifmerge after both
    // branches returned). codegen leaves that block open; we close it so
    // the output always assembles.
    if (!p.terminated) terminator(p, "ret i32 0");

    p.out += "}\n";
}

} // namespace

std::string irprint_module(ASTNode *ast) {
    assert(ast && ast->kind == NODE_PROGRAM);

    Printer p;
    p.out += "; ModuleID = 'main'\n";
    p.out += "declare i32 @print_int(i32)\n";
    p.out += "declare i32 @print_bool(i32)\n";
    p.out += "declare i32 @print_char(i32)\n";

    for (int i = 0; i < ast->data.program.count; i++) {
        p.out += "\n";
        emit_function(p, ast->data.program.functions[i]);
    }
    return p.out;
}
