// codegen.cpp -- AST -> LLVM IR -> object file, using the LLVM C++ API.
//
// Same shape as the previous C-API codegen: two-pass per program (declare
// every function first, then emit bodies), one IR instruction per AST node,
// then run the -O1 pass pipeline and emit either a .o or a .ll. The only
// substantive change vs. the C API is the *spelling* -- IRBuilder<> instead
// of LLVMBuildXxx, std::unique_ptr instead of manual disposal, the new
// PassManager incantation instead of LLVMRunPasses("default<O1>").

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>

#include "codegen.h"

using namespace llvm;

namespace {

// One LLVM context per codegen() invocation. Owns the type uniquing tables.
// Module + IRBuilder + the symbol stack are scoped to a single compilation.

// Symbol table: variable name -> the alloca that holds its slot. A flat
// vector with shadowing via "save the count on block entry, restore on
// exit", same trick the C version used.
struct Slot { std::string name; AllocaInst *alloca; };
using SymStack = std::vector<Slot>;

static AllocaInst *sym_lookup(const SymStack &st, const char *name) {
    // Reverse scan so the innermost binding wins (shadowing).
    for (auto it = st.rbegin(); it != st.rend(); ++it)
        if (it->name == name) return it->alloca;
    return nullptr;
}

static void sym_add(SymStack &st, const char *name, AllocaInst *a) {
    st.push_back({name, a});
}

// The i1 -> i32 widening shows up in every comparison / logical / unary-not.
static Value *zext_i1_to_i32(IRBuilder<> &b, Value *v, const char *name) {
    return b.CreateZExt(v, b.getInt32Ty(), name);
}

static Value *gen_expr(IRBuilder<> &b, Module &mod, SymStack &st, ASTNode *node);

static Value *gen_unary(IRBuilder<> &b, Value *operand, UnaryOp op) {
    switch (op) {
    case OP_NEG:
        return b.CreateNeg(operand, "neg");
    case OP_NOT: {
        // !x  ==  (x == 0); widen the i1 result back to i32.
        Value *cmp = b.CreateICmpEQ(operand, b.getInt32(0), "not");
        return zext_i1_to_i32(b, cmp, "not_ext");
    }
    case OP_BITNOT:
        return b.CreateNot(operand, "bitnot");
    }
    assert(false && "gen_unary: unknown UnaryOp");
    return nullptr;
}

static Value *gen_binop(IRBuilder<> &b, Value *l, Value *r, BinaryOp op) {
    switch (op) {
    case OP_ADD: return b.CreateAdd(l, r, "add");
    case OP_SUB: return b.CreateSub(l, r, "sub");
    case OP_MUL: return b.CreateMul(l, r, "mul");
    case OP_DIV: return b.CreateSDiv(l, r, "div");
    case OP_MOD: return b.CreateSRem(l, r, "mod");

    case OP_LT: case OP_GT: case OP_LTE: case OP_GTE:
    case OP_EQ: case OP_NEQ: {
        Value *cmp = nullptr;
        switch (op) {
        case OP_LT:  cmp = b.CreateICmpSLT(l, r, "cmp"); break;
        case OP_GT:  cmp = b.CreateICmpSGT(l, r, "cmp"); break;
        case OP_LTE: cmp = b.CreateICmpSLE(l, r, "cmp"); break;
        case OP_GTE: cmp = b.CreateICmpSGE(l, r, "cmp"); break;
        case OP_EQ:  cmp = b.CreateICmpEQ (l, r, "cmp"); break;
        case OP_NEQ: cmp = b.CreateICmpNE (l, r, "cmp"); break;
        default: assert(false); break;
        }
        return zext_i1_to_i32(b, cmp, "cmp_ext");
    }

    case OP_AND: {
        // Bitwise AND of (l != 0) and (r != 0), widened back. Matches the
        // C version exactly -- this is *not* short-circuit; both sides have
        // already been evaluated by gen_expr.
        Value *lb = b.CreateICmpNE(l, b.getInt32(0), "l_bool");
        Value *rb = b.CreateICmpNE(r, b.getInt32(0), "r_bool");
        Value *res = b.CreateAnd(lb, rb, "and");
        return zext_i1_to_i32(b, res, "and_ext");
    }
    case OP_OR: {
        Value *lb = b.CreateICmpNE(l, b.getInt32(0), "l_bool");
        Value *rb = b.CreateICmpNE(r, b.getInt32(0), "r_bool");
        Value *res = b.CreateOr(lb, rb, "or");
        return zext_i1_to_i32(b, res, "or_ext");
    }
    }
    assert(false && "gen_binop: unknown BinaryOp");
    return nullptr;
}

static Value *gen_expr(IRBuilder<> &b, Module &mod, SymStack &st, ASTNode *node) {
    assert(node);
    switch (node->kind) {
    case NODE_INT_LIT:
        return b.getInt32(node->data.int_lit.value);
    case NODE_CHAR_LIT:
        return b.getInt32(node->data.char_lit.value);

    case NODE_UNARY_OP: {
        Value *operand = gen_expr(b, mod, st, node->data.unary.operand);
        if (!operand) return nullptr;
        return gen_unary(b, operand, node->data.unary.op);
    }

    case NODE_BINARY_OP: {
        Value *l = gen_expr(b, mod, st, node->data.bin.left);
        Value *r = gen_expr(b, mod, st, node->data.bin.right);
        if (!l || !r) return nullptr;
        return gen_binop(b, l, r, node->data.bin.op);
    }

    case NODE_VAR_REF: {
        assert(node->data.var_ref.name);
        AllocaInst *a = sym_lookup(st, node->data.var_ref.name);
        // Typechecker already verified the binding -- a miss is a compiler bug.
        assert(a && "codegen: var ref unbound after typecheck");
        // Opaque pointers (LLVM 15+): the load needs the element type explicitly.
        return b.CreateLoad(b.getInt32Ty(), a, node->data.var_ref.name);
    }

    case NODE_CALL: {
        assert(node->data.call.name);
        Function *callee = mod.getFunction(node->data.call.name);
        assert(callee && "codegen: callee not declared after typecheck");
        std::vector<Value *> args;
        args.reserve(node->data.call.arg_count);
        for (int i = 0; i < node->data.call.arg_count; i++) {
            Value *a = gen_expr(b, mod, st, node->data.call.args[i]);
            if (!a) return nullptr;
            args.push_back(a);
        }
        return b.CreateCall(callee, args, "call");
    }

    default:
        assert(false && "gen_expr: unhandled NodeKind in expression position");
        return nullptr;
    }
}

// Has the current insertion block already been closed by a terminator
// (ret/br/etc.)? Used to avoid emitting a redundant branch after a
// return inside an if/while body.
static bool block_has_terminator(IRBuilder<> &b) {
    return b.GetInsertBlock()->getTerminator() != nullptr;
}

// Build an i1 from an i32 condition (cond != 0).
static Value *cond_to_i1(IRBuilder<> &b, Value *v, const char *name) {
    return b.CreateICmpNE(v, b.getInt32(0), name);
}

static int gen_stmt(IRBuilder<> &b, Module &mod, SymStack &st, ASTNode *node);

static int gen_block(IRBuilder<> &b, Module &mod, SymStack &st, ASTNode *node) {
    // Save the symbol stack depth so anything declared inside falls out of
    // scope at block exit. The C version did this with `int saved = st->count`.
    size_t saved = st.size();
    for (int i = 0; i < node->data.block.count; i++) {
        if (gen_stmt(b, mod, st, node->data.block.stmts[i]))
            return 1;
    }
    st.resize(saved);
    return 0;
}

static int gen_stmt(IRBuilder<> &b, Module &mod, SymStack &st, ASTNode *node) {
    assert(node);
    switch (node->kind) {
    case NODE_RETURN: {
        Value *v = gen_expr(b, mod, st, node->data.ret.expr);
        if (!v) return 1;
        b.CreateRet(v);
        return 0;
    }

    case NODE_VAR_DECL: {
        assert(node->data.var_decl.name);
        AllocaInst *a = b.CreateAlloca(b.getInt32Ty(), nullptr,
                                       node->data.var_decl.name);
        sym_add(st, node->data.var_decl.name, a);
        if (node->data.var_decl.init) {
            Value *v = gen_expr(b, mod, st, node->data.var_decl.init);
            if (!v) return 1;
            b.CreateStore(v, a);
        }
        return 0;
    }

    case NODE_ASSIGN: {
        assert(node->data.assign.name);
        AllocaInst *a = sym_lookup(st, node->data.assign.name);
        assert(a && "codegen: assign to unbound name after typecheck");
        Value *v = gen_expr(b, mod, st, node->data.assign.value);
        if (!v) return 1;
        b.CreateStore(v, a);
        return 0;
    }

    case NODE_EXPR_STMT: {
        Value *v = gen_expr(b, mod, st, node->data.expr_stmt.expr);
        return v ? 0 : 1;
    }

    case NODE_BLOCK:
        return gen_block(b, mod, st, node);

    case NODE_IF: {
        Value *c = gen_expr(b, mod, st, node->data.if_stmt.cond);
        if (!c) return 1;
        Value *cmp = cond_to_i1(b, c, "ifcond");

        Function *func = b.GetInsertBlock()->getParent();
        LLVMContext &ctx = func->getContext();
        BasicBlock *then_bb  = BasicBlock::Create(ctx, "then",    func);
        BasicBlock *else_bb  = BasicBlock::Create(ctx, "else",    func);
        BasicBlock *merge_bb = BasicBlock::Create(ctx, "ifmerge", func);

        b.CreateCondBr(cmp, then_bb, else_bb);

        b.SetInsertPoint(then_bb);
        if (gen_stmt(b, mod, st, node->data.if_stmt.then_body)) return 1;
        if (!block_has_terminator(b)) b.CreateBr(merge_bb);

        b.SetInsertPoint(else_bb);
        if (node->data.if_stmt.else_body) {
            if (gen_stmt(b, mod, st, node->data.if_stmt.else_body)) return 1;
        }
        if (!block_has_terminator(b)) b.CreateBr(merge_bb);

        b.SetInsertPoint(merge_bb);
        return 0;
    }

    case NODE_WHILE: {
        Function *func = b.GetInsertBlock()->getParent();
        LLVMContext &ctx = func->getContext();
        BasicBlock *cond_bb  = BasicBlock::Create(ctx, "whilecond",  func);
        BasicBlock *body_bb  = BasicBlock::Create(ctx, "whilebody",  func);
        BasicBlock *after_bb = BasicBlock::Create(ctx, "whileafter", func);

        b.CreateBr(cond_bb);

        b.SetInsertPoint(cond_bb);
        Value *c = gen_expr(b, mod, st, node->data.while_stmt.cond);
        if (!c) return 1;
        Value *cmp = cond_to_i1(b, c, "whilecond");
        b.CreateCondBr(cmp, body_bb, after_bb);

        b.SetInsertPoint(body_bb);
        if (gen_stmt(b, mod, st, node->data.while_stmt.body)) return 1;
        if (!block_has_terminator(b)) b.CreateBr(cond_bb);

        b.SetInsertPoint(after_bb);
        return 0;
    }

    case NODE_FOR: {
        size_t saved = st.size();
        Function *func = b.GetInsertBlock()->getParent();
        LLVMContext &ctx = func->getContext();

        if (node->data.for_stmt.init) {
            if (gen_stmt(b, mod, st, node->data.for_stmt.init)) return 1;
        }

        BasicBlock *cond_bb  = BasicBlock::Create(ctx, "forcond",  func);
        BasicBlock *body_bb  = BasicBlock::Create(ctx, "forbody",  func);
        BasicBlock *post_bb  = BasicBlock::Create(ctx, "forpost",  func);
        BasicBlock *after_bb = BasicBlock::Create(ctx, "forafter", func);

        b.CreateBr(cond_bb);

        b.SetInsertPoint(cond_bb);
        if (node->data.for_stmt.cond) {
            Value *c = gen_expr(b, mod, st, node->data.for_stmt.cond);
            if (!c) return 1;
            Value *cmp = cond_to_i1(b, c, "forcond");
            b.CreateCondBr(cmp, body_bb, after_bb);
        } else {
            b.CreateBr(body_bb);
        }

        b.SetInsertPoint(body_bb);
        if (gen_stmt(b, mod, st, node->data.for_stmt.body)) return 1;
        if (!block_has_terminator(b)) b.CreateBr(post_bb);

        b.SetInsertPoint(post_bb);
        if (node->data.for_stmt.post) {
            if (gen_stmt(b, mod, st, node->data.for_stmt.post)) return 1;
        }
        b.CreateBr(cond_bb);

        b.SetInsertPoint(after_bb);
        st.resize(saved);
        return 0;
    }

    default:
        assert(false && "gen_stmt: unhandled NodeKind in statement position");
        return 1;
    }
}

// Emit the object file via TargetMachine. The legacy PassManager is the
// supported path for codegen output even in modern LLVM (the new pass
// manager covers optimizations, not target lowering).
static int emit_object(Module &mod, TargetMachine &tm, const char *out_path) {
    std::error_code ec;
    raw_fd_ostream out(out_path, ec, sys::fs::OF_None);
    if (ec) {
        std::fprintf(stderr, "codegen: %s\n", ec.message().c_str());
        return 1;
    }
    legacy::PassManager pm;
    if (tm.addPassesToEmitFile(pm, out, /*DwoOut=*/nullptr,
                               CodeGenFileType::ObjectFile)) {
        std::fprintf(stderr, "codegen: target cannot emit object files\n");
        return 1;
    }
    pm.run(mod);
    out.flush();
    return 0;
}

// Run the same passes the C version ran ("function(mem2reg,instcombine,
// reassociate,gvn,simplifycfg,adce)") via the new PassManager.
static void run_optimization_passes(Module &mod, TargetMachine *tm) {
    LoopAnalysisManager     lam;
    FunctionAnalysisManager fam;
    CGSCCAnalysisManager    cgam;
    ModuleAnalysisManager   mam;

    PassBuilder pb(tm);
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);

    ModulePassManager mpm;
    if (auto err = pb.parsePassPipeline(
            mpm,
            "function(mem2reg,instcombine,reassociate,gvn,simplifycfg,adce)")) {
        std::fprintf(stderr, "codegen: pass pipeline: %s\n",
                     toString(std::move(err)).c_str());
        return;
    }
    mpm.run(mod, mam);
}

} // namespace

// No extern "C" needed: every caller (main.c, compiled with -x c++) is now C++.
int codegen(ASTNode *ast, const char *output_path, CodegenOptions *opts) {
    assert(ast);
    assert(ast->kind == NODE_PROGRAM);
    assert(output_path);

    // 1. Init LLVM.
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();

    // 2. Per-compilation owners.
    LLVMContext ctx;
    auto module = std::make_unique<Module>("main", ctx);
    IRBuilder<> builder(ctx);

    // Note: using llvm::Type explicitly because types.h's project-level `Type`
    // (the typechecker's notion of a source-language type) collides with
    // llvm::Type (the IR type) under `using namespace llvm`.
    llvm::Type *i32 = llvm::Type::getInt32Ty(ctx);

    // Pass 1a: declare the runtime built-ins. Bodies live in runtime/runtime.c,
    // resolved at link time.
    {
        FunctionType *i32_to_i32 = FunctionType::get(i32, {i32}, /*isVarArg=*/false);
        Function::Create(i32_to_i32, Function::ExternalLinkage, "print_int",  module.get());
        Function::Create(i32_to_i32, Function::ExternalLinkage, "print_bool", module.get());
        Function::Create(i32_to_i32, Function::ExternalLinkage, "print_char", module.get());
    }

    int func_count = ast->data.program.count;
    ASTNode **functions = ast->data.program.functions;

    // Pass 1b: declare every user function up front so call sites resolve
    // regardless of source order.
    for (int i = 0; i < func_count; i++) {
        ASTNode *fn = functions[i];
        std::vector<llvm::Type *> param_types(fn->data.func.param_count, i32);
        FunctionType *ft = FunctionType::get(i32, param_types, /*isVarArg=*/false);
        Function::Create(ft, Function::ExternalLinkage, fn->data.func.name, module.get());
    }

    // Pass 2: emit each function body.
    for (int i = 0; i < func_count; i++) {
        ASTNode *fn = functions[i];
        assert(fn && fn->kind == NODE_FUNCTION);
        Function *func = module->getFunction(fn->data.func.name);
        assert(func && "codegen: function missing after pass 1b");

        BasicBlock *entry = BasicBlock::Create(ctx, "entry", func);
        builder.SetInsertPoint(entry);

        SymStack st;
        // Alloca + store each parameter so it lives in a slot the rest of
        // the body can load/store, same shape as locals.
        for (int j = 0; j < fn->data.func.param_count; j++) {
            Value *param = func->getArg(j);
            AllocaInst *a = builder.CreateAlloca(i32, nullptr,
                                                 fn->data.func.params[j].name);
            builder.CreateStore(param, a);
            sym_add(st, fn->data.func.params[j].name, a);
        }

        if (gen_stmt(builder, *module, st, fn->data.func.body))
            return 1;
    }

    // 3. Optimization pipeline (mem2reg gateway + standard cleanups).
    // Build a TargetMachine once -- both the optimizer (for target-aware
    // analyses) and the object emitter need it.
    std::string triple = sys::getDefaultTargetTriple();
    std::string err;
    const Target *target = TargetRegistry::lookupTarget(triple, err);
    if (!target) {
        std::fprintf(stderr, "codegen: %s\n", err.c_str());
        return 1;
    }
    TargetOptions topts;
    std::unique_ptr<TargetMachine> tm(target->createTargetMachine(
        triple, "generic", "", topts, Reloc::PIC_));
    module->setDataLayout(tm->createDataLayout());
    module->setTargetTriple(triple);

    if (opts && opts->opt_level >= 1)
        run_optimization_passes(*module, tm.get());

    // 4. Emit either the textual IR or the object file.
    if (opts && opts->emit_llvm) {
        std::error_code ec;
        raw_fd_ostream out(output_path, ec, sys::fs::OF_None);
        if (ec) {
            std::fprintf(stderr, "codegen: %s\n", ec.message().c_str());
            return 1;
        }
        module->print(out, nullptr);
        return 0;
    }
    return emit_object(*module, *tm, output_path);
}
