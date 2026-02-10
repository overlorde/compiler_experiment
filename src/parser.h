#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

/*
 * Parser — recursive descent.
 *
 * Phase 1 grammar (minimal):
 *   program    → function
 *   function   → "int" "main" "(" ")" "{" statement "}"
 *   statement  → "return" expression ";"
 *   expression → INT_LIT
 *
 * You'll expand the grammar with each phase.
 */

typedef struct {
    Lexer lexer;
    Token current;  /* current token (look-ahead) */
} Parser;

/* Initialize parser with source text */
void parser_init(Parser *parser, const char *source);

/* Parse a complete program — returns the root AST node */
ASTNode *parser_parse(Parser *parser);

#endif
