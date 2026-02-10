#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#include "codegen.h"

/* --- Symbol table --- */

#define SYMTAB_MAX 256

typedef struct {
    struct { const char *name; LLVMValueRef alloca; } entries[SYMTAB_MAX];
    int count;
} SymbolTable;

static void symtab_init(SymbolTable *st) {
    st->count = 0;
}

static void symtab_add(SymbolTable *st, const char *name, LLVMValueRef alloca) {
    if (st->count >= SYMTAB_MAX) {
        fprintf(stderr, "codegen: symbol table full\n");
        return;
    }
    st->entries[st->count].name = name;
    st->entries[st->count].alloca = alloca;
    st->count++;
}

static LLVMValueRef symtab_lookup(SymbolTable *st, const char *name) {
    /* reverse search for shadowing support */
    for (int i = st->count - 1; i >= 0; i--) {
        if (strcmp(st->entries[i].name, name) == 0)
            return st->entries[i].alloca;
    }
    fprintf(stderr, "codegen: undefined variable '%s'\n", name);
    return NULL;
}

/* Module ref needed for function lookups in codegen_expr */
static LLVMModuleRef g_module;

/* Walk the AST and produce an LLVM value */
static LLVMValueRef codegen_expr(LLVMBuilderRef builder, SymbolTable *st, ASTNode *node) {
    switch (node->kind) {
        case NODE_INT_LIT:
            return LLVMConstInt(LLVMInt32Type(), node->data.int_lit.value, 0);

        case NODE_UNARY_OP: {
            LLVMValueRef operand = codegen_expr(builder, st, node->data.unary.operand);
            if (!operand) return NULL;
            switch (node->data.unary.op) {
                case OP_NEG:
                    return LLVMBuildNeg(builder, operand, "neg");
                case OP_NOT: {
                    LLVMValueRef zero = LLVMConstInt(LLVMInt32Type(), 0, 0);
                    LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntEQ, operand, zero, "not");
                    return LLVMBuildZExt(builder, cmp, LLVMInt32Type(), "not_ext");
                }
                case OP_BITNOT:
                    return LLVMBuildNot(builder, operand, "bitnot");
            }
            return NULL;
        }

        case NODE_BINARY_OP: {
            LLVMValueRef left = codegen_expr(builder, st, node->data.bin.left);
            LLVMValueRef right = codegen_expr(builder, st, node->data.bin.right);
            if (!left || !right) return NULL;

            switch (node->data.bin.op) {
                case OP_ADD: return LLVMBuildAdd(builder, left, right, "add");
                case OP_SUB: return LLVMBuildSub(builder, left, right, "sub");
                case OP_MUL: return LLVMBuildMul(builder, left, right, "mul");
                case OP_DIV: return LLVMBuildSDiv(builder, left, right, "div");
                case OP_MOD: return LLVMBuildSRem(builder, left, right, "mod");

                case OP_LT: case OP_GT: case OP_LTE: case OP_GTE:
                case OP_EQ: case OP_NEQ: {
                    LLVMIntPredicate pred;
                    switch (node->data.bin.op) {
                        case OP_LT:  pred = LLVMIntSLT; break;
                        case OP_GT:  pred = LLVMIntSGT; break;
                        case OP_LTE: pred = LLVMIntSLE; break;
                        case OP_GTE: pred = LLVMIntSGE; break;
                        case OP_EQ:  pred = LLVMIntEQ;  break;
                        case OP_NEQ: pred = LLVMIntNE;  break;
                        default:     pred = LLVMIntEQ;   break;
                    }
                    LLVMValueRef cmp = LLVMBuildICmp(builder, pred, left, right, "cmp");
                    return LLVMBuildZExt(builder, cmp, LLVMInt32Type(), "cmp_ext");
                }

                case OP_AND: {
                    LLVMValueRef zero = LLVMConstInt(LLVMInt32Type(), 0, 0);
                    LLVMValueRef l = LLVMBuildICmp(builder, LLVMIntNE, left, zero, "l_bool");
                    LLVMValueRef r = LLVMBuildICmp(builder, LLVMIntNE, right, zero, "r_bool");
                    LLVMValueRef result = LLVMBuildAnd(builder, l, r, "and");
                    return LLVMBuildZExt(builder, result, LLVMInt32Type(), "and_ext");
                }

                case OP_OR: {
                    LLVMValueRef zero = LLVMConstInt(LLVMInt32Type(), 0, 0);
                    LLVMValueRef l = LLVMBuildICmp(builder, LLVMIntNE, left, zero, "l_bool");
                    LLVMValueRef r = LLVMBuildICmp(builder, LLVMIntNE, right, zero, "r_bool");
                    LLVMValueRef result = LLVMBuildOr(builder, l, r, "or");
                    return LLVMBuildZExt(builder, result, LLVMInt32Type(), "or_ext");
                }
            }
            return NULL;
        }

        case NODE_VAR_REF: {
            LLVMValueRef alloca = symtab_lookup(st, node->data.var_ref.name);
            if (!alloca) return NULL;
            return LLVMBuildLoad2(builder, LLVMInt32Type(), alloca, node->data.var_ref.name);
        }

        case NODE_CALL: {
            LLVMValueRef callee = LLVMGetNamedFunction(g_module, node->data.call.name);
            if (!callee) {
                fprintf(stderr, "codegen: undefined function '%s'\n", node->data.call.name);
                return NULL;
            }
            int nargs = node->data.call.arg_count;
            LLVMValueRef *args = malloc(nargs * sizeof(LLVMValueRef));
            for (int i = 0; i < nargs; i++) {
                args[i] = codegen_expr(builder, st, node->data.call.args[i]);
                if (!args[i]) { free(args); return NULL; }
            }
            LLVMTypeRef callee_type = LLVMGlobalGetValueType(callee);
            LLVMValueRef result = LLVMBuildCall2(builder, callee_type, callee, args, nargs, "call");
            free(args);
            return result;
        }

        default:
            fprintf(stderr, "codegen: unexpected node kind %d in expression\n", node->kind);
            return NULL;
    }
}

/* Check if the current basic block already has a terminator */
static int block_has_terminator(LLVMBuilderRef builder) {
    LLVMBasicBlockRef bb = LLVMGetInsertBlock(builder);
    return LLVMGetBasicBlockTerminator(bb) != NULL;
}

/* Emit a statement. Returns 0 on success, non-zero on error. */
static int codegen_stmt(LLVMBuilderRef builder, SymbolTable *st, ASTNode *node) {
    switch (node->kind) {
        case NODE_RETURN: {
            LLVMValueRef val = codegen_expr(builder, st, node->data.ret.expr);
            if (!val) return 1;
            LLVMBuildRet(builder, val);
            return 0;
        }

        case NODE_VAR_DECL: {
            LLVMValueRef alloca = LLVMBuildAlloca(builder, LLVMInt32Type(),
                                                   node->data.var_decl.name);
            symtab_add(st, node->data.var_decl.name, alloca);
            if (node->data.var_decl.init) {
                LLVMValueRef val = codegen_expr(builder, st, node->data.var_decl.init);
                if (!val) return 1;
                LLVMBuildStore(builder, val, alloca);
            }
            return 0;
        }

        case NODE_ASSIGN: {
            LLVMValueRef alloca = symtab_lookup(st, node->data.assign.name);
            if (!alloca) return 1;
            LLVMValueRef val = codegen_expr(builder, st, node->data.assign.value);
            if (!val) return 1;
            LLVMBuildStore(builder, val, alloca);
            return 0;
        }

        case NODE_EXPR_STMT: {
            LLVMValueRef val = codegen_expr(builder, st, node->data.expr_stmt.expr);
            if (!val) return 1;
            return 0;
        }

        case NODE_BLOCK: {
            int saved = st->count;
            for (int i = 0; i < node->data.block.count; i++) {
                if (codegen_stmt(builder, st, node->data.block.stmts[i]))
                    return 1;
            }
            st->count = saved;
            return 0;
        }

        case NODE_IF: {
            LLVMValueRef cond_val = codegen_expr(builder, st, node->data.if_stmt.cond);
            if (!cond_val) return 1;
            /* Convert to i1: cond != 0 */
            LLVMValueRef zero = LLVMConstInt(LLVMInt32Type(), 0, 0);
            LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntNE, cond_val, zero, "ifcond");

            LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
            LLVMBasicBlockRef then_bb = LLVMAppendBasicBlock(func, "then");
            LLVMBasicBlockRef else_bb = LLVMAppendBasicBlock(func, "else");
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlock(func, "ifmerge");

            LLVMBuildCondBr(builder, cmp, then_bb, else_bb);

            /* then */
            LLVMPositionBuilderAtEnd(builder, then_bb);
            if (codegen_stmt(builder, st, node->data.if_stmt.then_body)) return 1;
            if (!block_has_terminator(builder))
                LLVMBuildBr(builder, merge_bb);

            /* else */
            LLVMPositionBuilderAtEnd(builder, else_bb);
            if (node->data.if_stmt.else_body) {
                if (codegen_stmt(builder, st, node->data.if_stmt.else_body)) return 1;
            }
            if (!block_has_terminator(builder))
                LLVMBuildBr(builder, merge_bb);

            LLVMPositionBuilderAtEnd(builder, merge_bb);
            return 0;
        }

        case NODE_WHILE: {
            LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));
            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlock(func, "whilecond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlock(func, "whilebody");
            LLVMBasicBlockRef after_bb = LLVMAppendBasicBlock(func, "whileafter");

            LLVMBuildBr(builder, cond_bb);

            /* cond */
            LLVMPositionBuilderAtEnd(builder, cond_bb);
            LLVMValueRef cond_val = codegen_expr(builder, st, node->data.while_stmt.cond);
            if (!cond_val) return 1;
            LLVMValueRef zero = LLVMConstInt(LLVMInt32Type(), 0, 0);
            LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntNE, cond_val, zero, "whilecond");
            LLVMBuildCondBr(builder, cmp, body_bb, after_bb);

            /* body */
            LLVMPositionBuilderAtEnd(builder, body_bb);
            if (codegen_stmt(builder, st, node->data.while_stmt.body)) return 1;
            if (!block_has_terminator(builder))
                LLVMBuildBr(builder, cond_bb);

            LLVMPositionBuilderAtEnd(builder, after_bb);
            return 0;
        }

        case NODE_FOR: {
            int saved = st->count;
            LLVMValueRef func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder));

            /* init */
            if (node->data.for_stmt.init) {
                if (codegen_stmt(builder, st, node->data.for_stmt.init)) return 1;
            }

            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlock(func, "forcond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlock(func, "forbody");
            LLVMBasicBlockRef post_bb = LLVMAppendBasicBlock(func, "forpost");
            LLVMBasicBlockRef after_bb = LLVMAppendBasicBlock(func, "forafter");

            LLVMBuildBr(builder, cond_bb);

            /* cond */
            LLVMPositionBuilderAtEnd(builder, cond_bb);
            if (node->data.for_stmt.cond) {
                LLVMValueRef cond_val = codegen_expr(builder, st, node->data.for_stmt.cond);
                if (!cond_val) return 1;
                LLVMValueRef zero = LLVMConstInt(LLVMInt32Type(), 0, 0);
                LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntNE, cond_val, zero, "forcond");
                LLVMBuildCondBr(builder, cmp, body_bb, after_bb);
            } else {
                LLVMBuildBr(builder, body_bb);
            }

            /* body */
            LLVMPositionBuilderAtEnd(builder, body_bb);
            if (codegen_stmt(builder, st, node->data.for_stmt.body)) return 1;
            if (!block_has_terminator(builder))
                LLVMBuildBr(builder, post_bb);

            /* post */
            LLVMPositionBuilderAtEnd(builder, post_bb);
            if (node->data.for_stmt.post) {
                if (codegen_stmt(builder, st, node->data.for_stmt.post)) return 1;
            }
            LLVMBuildBr(builder, cond_bb);

            LLVMPositionBuilderAtEnd(builder, after_bb);
            st->count = saved;
            return 0;
        }

        default:
            fprintf(stderr, "codegen: unexpected node kind %d in statement\n", node->kind);
            return 1;
    }
}

static int emit_object(LLVMModuleRef mod, const char *output_path) {
    char *triple = LLVMGetDefaultTargetTriple();

    LLVMTargetRef target;
    char *error = NULL;
    if (LLVMGetTargetFromTriple(triple, &target, &error)) {
        fprintf(stderr, "codegen: %s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(triple);
        return 1;
    }

    LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
        target, triple, "generic", "",
        LLVMCodeGenLevelDefault,
        LLVMRelocPIC,
        LLVMCodeModelDefault
    );
    LLVMDisposeMessage(triple);

    if (LLVMTargetMachineEmitToFile(machine, mod, (char *)output_path,
                                     LLVMObjectFile, &error)) {
        fprintf(stderr, "codegen: %s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeTargetMachine(machine);
        return 1;
    }

    LLVMDisposeTargetMachine(machine);
    return 0;
}

int codegen(ASTNode *ast, const char *output_path) {
    /* 1. Init LLVM */
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();

    /* 2. Create module */
    LLVMModuleRef mod = LLVMModuleCreateWithName("main");
    g_module = mod;

    LLVMBuilderRef builder = LLVMCreateBuilder();
    int func_count = ast->data.program.count;
    ASTNode **functions = ast->data.program.functions;

    /* Pass 1: declare all functions */
    for (int i = 0; i < func_count; i++) {
        ASTNode *fn = functions[i];
        int nparams = fn->data.func.param_count;
        LLVMTypeRef *param_types = malloc(nparams * sizeof(LLVMTypeRef));
        for (int j = 0; j < nparams; j++)
            param_types[j] = LLVMInt32Type();
        LLVMTypeRef ftype = LLVMFunctionType(LLVMInt32Type(), param_types, nparams, 0);
        LLVMAddFunction(mod, fn->data.func.name, ftype);
        free(param_types);
    }

    /* Pass 2: emit bodies */
    for (int i = 0; i < func_count; i++) {
        ASTNode *fn = functions[i];
        LLVMValueRef func = LLVMGetNamedFunction(mod, fn->data.func.name);

        LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
        LLVMPositionBuilderAtEnd(builder, entry);

        SymbolTable st;
        symtab_init(&st);

        /* Alloca + store each parameter */
        for (int j = 0; j < fn->data.func.param_count; j++) {
            LLVMValueRef param = LLVMGetParam(func, j);
            LLVMValueRef alloca = LLVMBuildAlloca(builder, LLVMInt32Type(),
                                                   fn->data.func.params[j]);
            LLVMBuildStore(builder, param, alloca);
            symtab_add(&st, fn->data.func.params[j], alloca);
        }

        if (codegen_stmt(builder, &st, fn->data.func.body)) {
            LLVMDisposeBuilder(builder);
            LLVMDisposeModule(mod);
            return 1;
        }
    }

    /* 3. Emit object file */
    int result = emit_object(mod, output_path);

    /* 4. Cleanup */
    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(mod);
    return result;
}
