#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"

typedef struct {
    int emit_llvm;   /* 1 = write .ll text IR, 0 = write .o */
    int opt_level;   /* 0 = no passes, 1 = basic optimization passes */
} CodegenOptions;

/* Compile an AST to an object file or LLVM IR. Returns 0 on success. */
int codegen(ASTNode *ast, const char *output_path, CodegenOptions *opts);

#endif
