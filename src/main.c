#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "codegen.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "error: cannot open '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    CodegenOptions opts = {0, 0};
    int i;

    /* Parse flags */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') break;
        if (strcmp(argv[i], "-emit-llvm") == 0)
            opts.emit_llvm = 1;
        else if (strcmp(argv[i], "-O1") == 0)
            opts.opt_level = 1;
        else {
            fprintf(stderr, "error: unknown flag '%s'\n", argv[i]);
            return 1;
        }
    }

    if (argc - i != 2) {
        fprintf(stderr, "Usage: %s [flags] <input.c> <output>\n", argv[0]);
        fprintf(stderr, "Flags: -emit-llvm  -O1\n");
        return 1;
    }

    const char *input_path  = argv[i];
    const char *output_path = argv[i + 1];

    /* Read source file */
    char *source = read_file(input_path);
    if (!source) return 1;

    /* Parse */
    Parser parser;
    parser_init(&parser, source);
    ASTNode *ast = parser_parse(&parser);
    if (!ast) {
        fprintf(stderr, "error: parsing failed\n");
        free(source);
        return 1;
    }

    /* Codegen */
    int result = codegen(ast, output_path, &opts);

    /* Cleanup */
    ast_free(ast);
    free(source);

    return result;
}
