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
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.c> <output.o>\n", argv[0]);
        return 1;
    }

    const char *input_path  = argv[1];
    const char *output_path = argv[2];

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
    int result = codegen(ast, output_path);

    /* Cleanup */
    ast_free(ast);
    free(source);

    return result;
}
