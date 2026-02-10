#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"

/*
 * Code generation — AST to LLVM IR to object file.
 *
 * Uses the LLVM-C API (llvm-c/Core.h, llvm-c/TargetMachine.h, etc.)
 *
 * Key LLVM-C functions you'll need for Phase 1:
 *   LLVMModuleCreateWithName()
 *   LLVMFunctionType(), LLVMAddFunction()
 *   LLVMAppendBasicBlock()
 *   LLVMCreateBuilder(), LLVMPositionBuilderAtEnd()
 *   LLVMConstInt()
 *   LLVMBuildRet()
 *   LLVMPrintModuleToString()  — for debugging
 *   LLVMWriteBitcodeToFile() or target machine emit
 */

/* Compile an AST to an object file. Returns 0 on success, non-zero on error. */
int codegen(ASTNode *ast, const char *output_path);

#endif
