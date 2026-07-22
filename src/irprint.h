#ifndef IRPRINT_H
#define IRPRINT_H

#include <string>

#include "ast.h"

/*
 * Textual LLVM IR emitter -- codegen.cpp's sibling, with no LLVM dependency.
 *
 * Walks the same typed AST with the same lowering decisions (everything is
 * i32, every local an alloca, loads/stores everywhere, zext-widened
 * comparisons) but prints .ll text instead of calling the LLVM API. This is
 * what the wasm build ships: producing IR text needs none of LLVM's
 * machinery, only consuming it does.
 *
 * The one deliberate divergence from codegen.cpp: a block left without a
 * terminator (e.g. a function whose control flow can fall off the end) gets
 * a trailing `ret i32 0`, so the output always satisfies llvm-as.
 *
 * Call only after typechecking succeeded -- like codegen, it treats unbound
 * names as compiler bugs, not user errors.
 */
std::string irprint_module(ASTNode *ast);

#endif
