#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "error.h"
#include "parser.h"
#include "regions.h"
#include "scopes.h"
#include "typecheck.h"

extern "C" const char *ce_compile(const char *source);
static int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); \
} } while (0)

static void capture(void *data, const char *message) {
    *static_cast<std::string *>(data) += std::string(message) + "\n";
}

struct Program {
    ASTNode *ast;
    SymTab types, functions, vars, ops;
    std::string errors;
    explicit Program(const char *source) {
        error_reset(); error_set_sink(capture, &errors);
        Parser parser; parser_init(&parser, source); ast = parser_parse(&parser);
        CHECK(ast && error_count() == 0);
        symtab_init(&types, nullptr); symtab_init(&functions, nullptr);
        symtab_init(&vars, nullptr); symtab_init(&ops, nullptr);
        typecheck_register_builtins(&functions);
        scopes_build(ast, &vars, nullptr);
        int result = typecheck(ast, &types, &functions, &vars, &ops);
        if (result) std::fprintf(stderr, "%s", errors.c_str());
        CHECK(result == 0);
        error_set_sink(nullptr, nullptr);
    }
    ASTNode *function(const char *name) {
        return static_cast<ASTNode *>(symtab_lookup(&functions, name));
    }
    ~Program() { ast_free(ast); }
};

int main() {
    {
        Program p("main() { print_int(id(42)); print_char(id('A')); print_bool(id(true)); return 0; }"
                  "id(x) { return x; }");
        ASTNode *id = p.function("id");
        CHECK(type_resolve(id->data.func.ret_type)->kind == TYPE_VAR);
        CHECK(type_equals(id->data.func.ret_type, id->data.func.params[0].type));
        CHECK(id->data.func.region_policy == REGION_CALLER);
        CHECK(p.function("main")->data.func.region_policy == REGION_LOCAL);
    }
    {
        Program p("first(n,x) { if(n==0) return x; return second(n-1,x); }"
                  "second(n,x) { if(n==0) return x; return first(n-1,x); }"
                  "main() { print_int(first(2,42)); print_char(second(3,'A')); return 0; }");
        CHECK(type_equals(p.function("first")->data.func.params[0].type, type_int()));
        CHECK(type_resolve(p.function("first")->data.func.ret_type)->kind == TYPE_VAR);
        CHECK(type_equals(p.function("first")->data.func.ret_type,
                          p.function("second")->data.func.ret_type));
    }
    {
        Program p("cell() { return alloc(8); }"
                  "scratch() { let p=cell(); store(p,42); return load(p); }"
                  "main() { print_int(scratch()); return 0; }");
        CHECK(p.function("cell")->data.func.region_policy == REGION_CALLER);
        CHECK(p.function("scratch")->data.func.region_policy == REGION_LOCAL);
        CHECK(type_resolve(p.function("cell")->data.func.ret_type)->param->kind == TYPE_VAR);
    }
    {
        Program p("publish(p,x) { store(p,x); return 0; }"
                  "relay(p,x) { return publish(p,x); }"
                  "main() { let p=alloc(8); let q=alloc(8); store(q,42); relay(p,q); return 0; }");
        CHECK(p.function("publish")->data.func.region_policy == REGION_ROOT);
        CHECK(p.function("relay")->data.func.region_policy == REGION_ROOT);
        CHECK(p.function("main")->data.func.region_policy == REGION_ROOT);
    }
    {
        Program p("a(n,p,x) { if(n==0) { store(p,x); return 0; } return b(n-1,p,x); }"
                  "b(n,p,x) { return a(n,p,x); } main() { return 0; }");
        CHECK(p.function("a")->data.func.region_policy == REGION_ROOT);
        CHECK(p.function("b")->data.func.region_policy == REGION_ROOT);
    }
    // The playground compiles repeatedly in one process. No signature links,
    // sinks, or region policies may bleed into the next compilation.
    for (int i = 0; i < 4; ++i) {
        std::string good = ce_compile("id(x){return x;} main(){print_int(id(42));print_char(id('A'));return 0;}");
        CHECK(good.find("OK\n") == 0);
        CHECK(good.find("@__ce_region_enter(i64 1)") != std::string::npos);
        CHECK(std::string(ce_compile("main(){let x=true;print_int(x);return 0;}")).find("ERR\n") == 0);
        CHECK(std::string(ce_compile("main(){let x=;}")).find("ERR\n") == 0);
        CHECK(std::string(ce_compile("main(){return 0;}")).find("OK\n") == 0);
    }
    std::printf("Frontend tests: %d checks passed\n", checks);
}
