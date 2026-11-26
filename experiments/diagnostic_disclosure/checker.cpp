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

struct Snapshot {
    int line;
    std::string context, expected, actual, public_expected, public_actual;
};
struct Capture { std::string text; std::vector<Snapshot> diagnostics; };

static std::string public_name(const Type *t) {
    t = type_resolve(t);
    if (!t) return "unknown";
    switch (t->kind) {
    case TYPE_INT: return "int";
    case TYPE_BOOL: return "bool";
    case TYPE_CHAR: return "char";
    case TYPE_NUMBER: return "number";
    case TYPE_PTR: return t->param ? "ptr<" + public_name(t->param) + ">" : "ptr";
    case TYPE_VAR: return "unknown";
    }
    return "unknown";
}
static void text_sink(void *data, const char *message) {
    static_cast<Capture *>(data)->text += std::string(message) + "\n";
}
static void structured_sink(void *data, const Diagnostic *d) {
    static_cast<Capture *>(data)->diagnostics.push_back({d->line, d->context,
        type_name(d->expected), type_name(d->actual),
        public_name(d->expected), public_name(d->actual)});
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
static void redact(std::string &s, const std::vector<std::string> &secrets) {
    // Registry entries have equal length in this pilot (thus also longest-first).
    for (const auto &secret : secrets) {
        size_t pos = 0;
        while ((pos = s.find(secret, pos)) != std::string::npos) {
            s.replace(pos, secret.size(), "[private]"); pos += 9;
        }
    }
}
int main(int argc, char **argv) {
    if (argc != 5) return 2;
    std::string stratum = argv[1], mode = argv[4];
    char *end = nullptr;
    long world = std::strtol(argv[2], &end, 10);
    if (!*argv[2] || *end || world < 0 || world > 15) return 2;
    std::string shape = argv[3];
    if ((stratum != "metadata" && stratum != "contracts") ||
        (shape != "scalar" && shape != "pointer") ||
        (mode != "full" && mode != "redacted" && mode != "generic" && mode != "projected")) return 2;
    std::string source((std::istreambuf_iterator<char>(std::cin)), {});
    Capture capture;
    error_reset(); error_set_sink(text_sink, &capture);
    error_set_diagnostic_sink(structured_sink, &capture);
    Parser parser; parser_init(&parser, source.c_str());
    ASTNode *ast = parser_parse(&parser);
    bool parsed = ast && error_count() == 0;
    bool accepted = false;
    std::vector<std::string> secrets;
    if (parsed) {
        SymTab types, funcs, vars, ops;
        symtab_init(&types, nullptr); symtab_init(&funcs, nullptr);
        symtab_init(&vars, nullptr); symtab_init(&ops, nullptr);
        typecheck_register_builtins(&funcs);
        Type aliases[4] = {}, pointers[4] = {};
        Param params[4] = {};
        ASTNode signatures[4] = {};
        std::string names[4], labels[4];
        for (int i = 0; i < 4; ++i) {
            names[i] = "probe" + std::to_string(i);
            char label[48];
            std::snprintf(label, sizeof label, "private_%08lx_%02d", 0x91ac0021UL + world * 7919, i);
            labels[i] = label; secrets.push_back(label);
            aliases[i] = *(stratum == "contracts" && (world & (1 << i)) ? type_bool() : type_int());
            if (stratum == "metadata") aliases[i].display = labels[i].c_str();
            pointers[i].kind = TYPE_PTR; pointers[i].param = &aliases[i];
            Type *t = shape == "pointer" ? &pointers[i] : &aliases[i];
            params[i] = {const_cast<char *>("x"), t};
            signatures[i].kind = NODE_FUNCTION;
            signatures[i].data.func.name = names[i].c_str();
            signatures[i].data.func.params = &params[i];
            signatures[i].data.func.param_count = 1;
            signatures[i].data.func.ret_type = stratum == "metadata" ? t : type_int();
            symtab_add(&funcs, names[i].c_str(), &signatures[i]);
        }
        scopes_build(ast, &vars, nullptr);
        accepted = typecheck(ast, &types, &funcs, &vars, &ops) == 0;
        // All borrowed type fields have already been snapshotted in the callbacks.
    }
    error_set_diagnostic_sink(nullptr, nullptr); error_set_sink(nullptr, nullptr);
    std::string feedback;
    std::vector<Snapshot> visible;
    if (accepted) feedback = "accepted";
    else if (mode == "generic" || (mode == "projected" && parsed && stratum == "contracts")) feedback = "rejected";
    else if (mode == "projected" && parsed) {
        // Any unstructured checker failure forces a generic response.
        if (capture.diagnostics.size() != static_cast<size_t>(error_count())) feedback = "rejected";
        else {
            for (auto d : capture.diagnostics) {
                d.expected = d.public_expected; d.actual = d.public_actual;
                feedback += "error: line " + std::to_string(d.line) + ": " + d.context +
                    ": expected " + d.expected + ", got " + d.actual + "\n";
                visible.push_back(d);
            }
        }
    } else {
        feedback = capture.text; visible = capture.diagnostics;
        if (mode == "redacted") {
            redact(feedback, secrets);
            for (auto &d : visible) { redact(d.expected, secrets); redact(d.actual, secrets); }
        }
    }
    std::cout << "{\"accepted\":" << (accepted ? "true" : "false")
              << ",\"feedback\":" << quote(feedback) << ",\"constraints\":[";
    for (size_t i = 0; i < visible.size(); ++i) {
        const auto &d = visible[i]; if (i) std::cout << ',';
        std::cout << "{\"code\":\"type_mismatch\",\"line\":" << d.line
                  << ",\"context\":" << quote(d.context)
                  << ",\"expected\":" << quote(d.expected)
                  << ",\"actual\":" << quote(d.actual) << '}';
    }
    std::cout << "]}\n";
    ast_free(ast);
    return 0;
}
