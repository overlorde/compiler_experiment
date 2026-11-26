#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include "parser.h"
#include "scopes.h"
#include "symtab.h"
#include "typecheck.h"
#include "error.h"

struct Fact { int line; std::string context, expected, actual, canonical_expected, canonical_actual; };
struct Capture { std::string text; std::vector<Fact> facts; };
static std::string canonical(const Type *t) {
    t = type_resolve(t);
    if (!t) return "unknown";
    switch (t->kind) {
    case TYPE_INT: return "int";
    case TYPE_BOOL: return "bool";
    case TYPE_CHAR: return "char";
    case TYPE_NUMBER: return "number";
    case TYPE_PTR: return t->param ? "ptr<" + canonical(t->param) + ">" : "ptr";
    case TYPE_VAR: return "unknown";
    }
    return "unknown";
}
static void text_sink(void *p, const char *s) { static_cast<Capture *>(p)->text += std::string(s) + "\n"; }
static void fact_sink(void *p, const Diagnostic *d) {
    static_cast<Capture *>(p)->facts.push_back({d->line, d->context, type_name(d->expected),
        type_name(d->actual), canonical(d->expected), canonical(d->actual)});
}
static std::string quote(const std::string &s) {
    std::string out = "\"";
    const char *hex = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += c;
    }
    return out + "\"";
}
static void redact(std::string &s, const std::vector<std::string> &labels) {
    for (const auto &label : labels) {
        size_t pos = 0;
        while ((pos = s.find(label, pos)) != std::string::npos) {
            s.replace(pos, label.size(), "[private]"); pos += 9;
        }
    }
}
static int number(const char *s, int limit) {
    char *end = nullptr;
    long n = std::strtol(s, &end, 10);
    return *s && !*end && n >= 0 && n < limit ? static_cast<int>(n) : -1;
}
int main(int argc, char **argv) {
    if (argc != 5) return 2;
    int world = number(argv[1], 16), variant = number(argv[2], 6);
    std::string shape = argv[3], mode = argv[4];
    if (world < 0 || variant < 0 || (shape != "scalar" && shape != "pointer") ||
        (mode != "full" && mode != "redacted" && mode != "generic" && mode != "projected")) return 2;
    static const int permutations[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    Type *primitives[3] = {type_int(), type_bool(), type_char()};
    Type aliases[3] = {}, pointers[3] = {};
    ASTNode functions[6] = {};
    Param params[3] = {};
    std::string names[6], labels[3];
    std::vector<std::string> registry;
    for (int i = 0; i < 3; ++i) {
        char name[48];
        std::snprintf(name, sizeof name, "private_%08x_%02d", 0x672b1001U + world * 104729U, i);
        labels[i] = name; registry.push_back(labels[i]);
        aliases[i] = *primitives[i]; aliases[i].display = labels[i].c_str();
        pointers[i].kind = TYPE_PTR; pointers[i].param = &aliases[i];
    }
    SymTab types, funcs, vars, ops;
    symtab_init(&types, nullptr); symtab_init(&funcs, nullptr);
    symtab_init(&vars, nullptr); symtab_init(&ops, nullptr);
    typecheck_register_builtins(&funcs);
    for (int i = 0; i < 3; ++i) {
        int produced = permutations[(variant + 1) % 6][i], consumed = permutations[variant][i];
        names[i] = "source" + std::to_string(i); names[i+3] = "sink" + std::to_string(i);
        functions[i].kind = functions[i+3].kind = NODE_FUNCTION;
        functions[i].data.func.name = names[i].c_str();
        functions[i].data.func.ret_type = shape == "pointer" ? &pointers[produced] : &aliases[produced];
        functions[i+3].data.func.name = names[i+3].c_str();
        params[i] = {const_cast<char *>("value"), shape == "pointer" ? &pointers[consumed] : &aliases[consumed]};
        functions[i+3].data.func.params = &params[i]; functions[i+3].data.func.param_count = 1;
        functions[i+3].data.func.ret_type = type_int();
        symtab_add(&funcs, names[i].c_str(), &functions[i]);
        symtab_add(&funcs, names[i+3].c_str(), &functions[i+3]);
    }
    std::string source((std::istreambuf_iterator<char>(std::cin)), {});
    Capture captured;
    error_reset(); error_set_sink(text_sink, &captured); error_set_diagnostic_sink(fact_sink, &captured);
    Parser parser; parser_init(&parser, source.c_str()); ASTNode *ast = parser_parse(&parser);
    bool parsed = ast && error_count() == 0;
    if (parsed) { scopes_build(ast, &vars, nullptr); typecheck(ast, &types, &funcs, &vars, &ops); }
    bool accepted = parsed && error_count() == 0;
    int count = error_count();
    error_set_sink(nullptr, nullptr); error_set_diagnostic_sink(nullptr, nullptr);
    std::string feedback;
    std::vector<Fact> visible;
    if (accepted) feedback = "accepted";
    else if (mode == "generic") feedback = "rejected";
    else if (mode == "projected" && parsed) {
        if (count != static_cast<int>(captured.facts.size())) feedback = "rejected";
        else for (auto f : captured.facts) {
            f.expected = f.canonical_expected; f.actual = f.canonical_actual;
            feedback += "error: line " + std::to_string(f.line) + ": " + f.context + ": expected " + f.expected + ", got " + f.actual + "\n";
            visible.push_back(f);
        }
    } else {
        feedback = captured.text; visible = captured.facts;
        if (mode == "redacted") {
            redact(feedback, registry);
            for (auto &f : visible) { redact(f.expected, registry); redact(f.actual, registry); }
        }
    }
    std::cout << "{\"accepted\":" << (accepted ? "true" : "false") << ",\"feedback\":" << quote(feedback) << ",\"constraints\":[";
    for (size_t i = 0; i < visible.size(); ++i) {
        const auto &f = visible[i]; if (i) std::cout << ',';
        std::cout << "{\"code\":\"type_mismatch\",\"line\":" << f.line << ",\"context\":" << quote(f.context)
                  << ",\"expected\":" << quote(f.expected) << ",\"actual\":" << quote(f.actual) << '}';
    }
    std::cout << "]}\n";
    ast_free(ast);
}
