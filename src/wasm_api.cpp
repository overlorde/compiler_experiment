/* wasm_api.cpp -- the playground's entry point into the compiler.
 *
 * One exported function: ce_compile(source) runs the full frontend
 * (lex, parse, scopes, typecheck) and the textual IR printer -- the whole
 * pipeline main.cpp runs except LLVM codegen, which the wasm build
 * deliberately excludes.
 *
 * Protocol with the JavaScript side: the returned string is either
 *   "OK\n<llvm ir>"      on success, or
 *   "ERR\n<messages>"    when anything was reported through the error sink.
 * JS splits on the first newline. The string lives in a static buffer that
 * each call overwrites, so there is nothing for the caller to free.
 *
 * Known leak: the AST's scope tables (malloc'd by scopes_build) are not
 * reclaimed by ast_free, so each call leaks a few hundred bytes. Fine for
 * a debounced editor session; revisit if it ever matters.
 *
 * Compiled into the native cc as well (it's harmless there) so the regular
 * build keeps it warning-clean and the native smoke test can call it. */

#include <string>

#include "ast.h"
#include "error.h"
#include "irprint.h"
#include "parser.h"
#include "scopes.h"
#include "symtab.h"
#include "typecheck.h"
#include "types.h"

namespace {

std::string result;

void buffer_sink(void *userdata, const char *msg) {
    std::string *buf = static_cast<std::string *>(userdata);
    *buf += msg;
    *buf += '\n';
}

} // namespace

extern "C" const char *ce_compile(const char *source) {
    std::string errbuf;
    error_reset();
    error_set_sink(buffer_sink, &errbuf);

    Parser parser;
    parser_init(&parser, source);
    ASTNode *ast = parser_parse(&parser);

    if (error_count() > 0 || !ast) {
        result = "ERR\n";
        result += errbuf.empty() ? "error: parsing failed\n" : errbuf;
        if (ast) ast_free(ast);
        error_set_sink(NULL, NULL);
        return result.c_str();
    }

    /* Same environment setup as main.cpp. */
    SymTab type_env, var_env, op_env, func_env;
    symtab_init(&type_env, NULL);
    symtab_init(&var_env,  NULL);
    symtab_init(&op_env,   NULL);
    symtab_init(&func_env, NULL);

    symtab_add(&type_env, "int",    type_int());
    symtab_add(&type_env, "bool",   type_bool());
    symtab_add(&type_env, "number", type_number());
    symtab_add(&type_env, "char",   type_char());

    typecheck_register_builtins(&func_env);
    scopes_build(ast, &var_env, NULL);

    int rc = typecheck(ast, &type_env, &func_env, &var_env, &op_env);
    if (rc || error_count() > 0) {
        result = "ERR\n" + errbuf;
        ast_free(ast);
        error_set_sink(NULL, NULL);
        return result.c_str();
    }

    result = "OK\n" + irprint_module(ast);
    ast_free(ast);
    error_set_sink(NULL, NULL);
    return result.c_str();
}
