#include "regions.h"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

static bool may_be_pointer(Type *t) {
    t = type_resolve(t);
    return !t || t->kind == TYPE_PTR || t->kind == TYPE_VAR;
}

void regions_analyze(ASTNode *program) {
    std::unordered_map<std::string, int> names;
    int count = program->data.program.count;
    auto functions = program->data.program.functions;
    for (int i = 0; i < count; ++i) names.emplace(functions[i]->data.func.name, i);
    std::vector<std::vector<int>> calls(count);
    for (int i = 0; i < count; ++i) {
        ASTNode *fn = functions[i];
        fn->data.func.region_policy = may_be_pointer(fn->data.func.ret_type)
                                   ? REGION_CALLER : REGION_LOCAL;
        ast_walk(fn->data.func.body, [&](ASTNode *n) {
            if (n->kind != NODE_CALL) return;
            auto callee = names.find(n->data.call.name);
            if (callee != names.end()) calls[i].push_back(callee->second);
            if (std::strcmp(n->data.call.name, "store") == 0 &&
                n->data.call.arg_count == 2 &&
                may_be_pointer(n->data.call.args[1]->inferred_type))
                fn->data.func.region_policy = REGION_ROOT;
        });
    }
    // A root-effect callee could publish a pointer supplied by its caller.
    // The caller's allocations must therefore also have root lifetime. The
    // fixed point covers arbitrarily deep calls and mutually recursive groups.
    bool changed;
    do {
        changed = false;
        for (int i = 0; i < count; ++i) {
            if (functions[i]->data.func.region_policy == REGION_ROOT) continue;
            for (int j : calls[i])
                if (functions[j]->data.func.region_policy == REGION_ROOT) {
                    functions[i]->data.func.region_policy = REGION_ROOT;
                    changed = true;
                    break;
                }
        }
    } while (changed);
}
