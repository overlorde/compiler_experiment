#include <stdio.h>
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

        default:
            fprintf(stderr, "codegen: unexpected node kind %d in expression\n", node->kind);
            return NULL;
    }
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

    /* 3. Create main function: int main(void) */
    LLVMTypeRef ret_type = LLVMInt32Type();
    LLVMTypeRef func_type = LLVMFunctionType(ret_type, NULL, 0, 0);
    LLVMValueRef func = LLVMAddFunction(mod, ast->data.func.name, func_type);

    /* 4. Entry block + builder */
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilder();
    LLVMPositionBuilderAtEnd(builder, entry);

    /* 5. Emit body */
    SymbolTable st;
    symtab_init(&st);
    if (codegen_stmt(builder, &st, ast->data.func.body)) {
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(mod);
        return 1;
    }

    /* 6. Emit object file */
    int result = emit_object(mod, output_path);

    /* 7. Cleanup */
    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(mod);
    return result;
}
