#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

/* --- Helper functions --- */

static void advance(Parser *p) {
    p->current = lexer_next(&p->lexer);
}

static int check(Parser *p, TokenKind kind) {
    return p->current.kind == kind;
}

static int expect(Parser *p, TokenKind kind) {
    if (!check(p, kind)) {
        fprintf(stderr, "error: line %d: expected '%s', got '%s'\n",
                p->current.line,
                token_kind_name(kind),
                token_kind_name(p->current.kind));
        return 0;
    }
    advance(p);
    return 1;
}

/* --- Grammar rules (expressions) --- */

static ASTNode *parse_expression(Parser *p);

/* primary → INT_LIT | IDENT | "(" expression ")" */
static ASTNode *parse_primary(Parser *p) {
    if (check(p, TOK_INT_LIT)) {
        int value = p->current.data.int_value;
        advance(p);
        return ast_int_lit(value);
    }
    if (check(p, TOK_IDENT)) {
        char *name = malloc(p->current.length + 1);
        memcpy(name, p->current.start, p->current.length);
        name[p->current.length] = '\0';
        advance(p);
        return ast_var_ref(name);
    }
    if (check(p, TOK_LPAREN)) {
        advance(p);
        ASTNode *expr = parse_expression(p);
        if (!expr) return NULL;
        if (!expect(p, TOK_RPAREN)) { ast_free(expr); return NULL; }
        return expr;
    }
    fprintf(stderr, "error: line %d: expected expression, got '%s'\n",
            p->current.line, token_kind_name(p->current.kind));
    return NULL;
}

/* unary → ("-" | "!" | "~") unary | primary */
static ASTNode *parse_unary(Parser *p) {
    if (check(p, TOK_MINUS)) {
        advance(p);
        ASTNode *operand = parse_unary(p);
        if (!operand) return NULL;
        return ast_unary_op(OP_NEG, operand);
    }
    if (check(p, TOK_BANG)) {
        advance(p);
        ASTNode *operand = parse_unary(p);
        if (!operand) return NULL;
        return ast_unary_op(OP_NOT, operand);
    }
    if (check(p, TOK_TILDE)) {
        advance(p);
        ASTNode *operand = parse_unary(p);
        if (!operand) return NULL;
        return ast_unary_op(OP_BITNOT, operand);
    }
    return parse_primary(p);
}

/* multiplicative → unary (("*" | "/" | "%") unary)* */
static ASTNode *parse_multiplicative(Parser *p) {
    ASTNode *left = parse_unary(p);
    if (!left) return NULL;

    while (check(p, TOK_STAR) || check(p, TOK_SLASH) || check(p, TOK_PERCENT)) {
        BinaryOp op = p->current.kind == TOK_STAR ? OP_MUL
                    : p->current.kind == TOK_SLASH ? OP_DIV : OP_MOD;
        advance(p);
        ASTNode *right = parse_unary(p);
        if (!right) { ast_free(left); return NULL; }
        left = ast_binary_op(op, left, right);
    }
    return left;
}

/* additive → multiplicative (("+" | "-") multiplicative)* */
static ASTNode *parse_additive(Parser *p) {
    ASTNode *left = parse_multiplicative(p);
    if (!left) return NULL;

    while (check(p, TOK_PLUS) || check(p, TOK_MINUS)) {
        BinaryOp op = p->current.kind == TOK_PLUS ? OP_ADD : OP_SUB;
        advance(p);
        ASTNode *right = parse_multiplicative(p);
        if (!right) { ast_free(left); return NULL; }
        left = ast_binary_op(op, left, right);
    }
    return left;
}

/* comparison → additive (("<" | ">" | "<=" | ">=") additive)* */
static ASTNode *parse_comparison(Parser *p) {
    ASTNode *left = parse_additive(p);
    if (!left) return NULL;

    while (check(p, TOK_LT) || check(p, TOK_GT) ||
           check(p, TOK_LTE) || check(p, TOK_GTE)) {
        BinaryOp op = p->current.kind == TOK_LT  ? OP_LT
                    : p->current.kind == TOK_GT  ? OP_GT
                    : p->current.kind == TOK_LTE ? OP_LTE : OP_GTE;
        advance(p);
        ASTNode *right = parse_additive(p);
        if (!right) { ast_free(left); return NULL; }
        left = ast_binary_op(op, left, right);
    }
    return left;
}

/* equality → comparison (("==" | "!=") comparison)* */
static ASTNode *parse_equality(Parser *p) {
    ASTNode *left = parse_comparison(p);
    if (!left) return NULL;

    while (check(p, TOK_EQ) || check(p, TOK_NEQ)) {
        BinaryOp op = p->current.kind == TOK_EQ ? OP_EQ : OP_NEQ;
        advance(p);
        ASTNode *right = parse_comparison(p);
        if (!right) { ast_free(left); return NULL; }
        left = ast_binary_op(op, left, right);
    }
    return left;
}

/* logical_and → equality ("&&" equality)* */
static ASTNode *parse_logical_and(Parser *p) {
    ASTNode *left = parse_equality(p);
    if (!left) return NULL;

    while (check(p, TOK_AND)) {
        advance(p);
        ASTNode *right = parse_equality(p);
        if (!right) { ast_free(left); return NULL; }
        left = ast_binary_op(OP_AND, left, right);
    }
    return left;
}

/* logical_or → logical_and ("||" logical_and)* */
static ASTNode *parse_logical_or(Parser *p) {
    ASTNode *left = parse_logical_and(p);
    if (!left) return NULL;

    while (check(p, TOK_OR)) {
        advance(p);
        ASTNode *right = parse_logical_and(p);
        if (!right) { ast_free(left); return NULL; }
        left = ast_binary_op(OP_OR, left, right);
    }
    return left;
}

/* expression → logical_or */
static ASTNode *parse_expression(Parser *p) {
    return parse_logical_or(p);
}

static ASTNode *parse_block(Parser *p);

/*
 * statement → "return" expr ";"
 *           | "int" IDENT ("=" expr)? ";"
 *           | IDENT "=" expr ";"
 *           | "{" statement* "}"
 *           | expr ";"
 */
static ASTNode *parse_statement(Parser *p) {
    /* return expr; */
    if (check(p, TOK_KW_RETURN)) {
        advance(p);
        ASTNode *expr = parse_expression(p);
        if (!expr) return NULL;
        if (!expect(p, TOK_SEMICOLON)) { ast_free(expr); return NULL; }
        return ast_return(expr);
    }

    /* int IDENT (= expr)?; */
    if (check(p, TOK_KW_INT)) {
        advance(p);
        if (!check(p, TOK_IDENT)) {
            fprintf(stderr, "error: line %d: expected variable name after 'int'\n",
                    p->current.line);
            return NULL;
        }
        char *name = malloc(p->current.length + 1);
        memcpy(name, p->current.start, p->current.length);
        name[p->current.length] = '\0';
        advance(p);

        ASTNode *init = NULL;
        if (check(p, TOK_ASSIGN)) {
            advance(p);
            init = parse_expression(p);
            if (!init) { free(name); return NULL; }
        }
        if (!expect(p, TOK_SEMICOLON)) {
            free(name);
            ast_free(init);
            return NULL;
        }
        return ast_var_decl(name, init);
    }

    /* nested block */
    if (check(p, TOK_LBRACE)) {
        return parse_block(p);
    }

    /* IDENT "=" expr ";"  vs  expr ";" */
    if (check(p, TOK_IDENT)) {
        /* peek ahead: if next token is '=', it's assignment */
        Token next = lexer_peek(&p->lexer);
        if (next.kind == TOK_ASSIGN) {
            char *name = malloc(p->current.length + 1);
            memcpy(name, p->current.start, p->current.length);
            name[p->current.length] = '\0';
            advance(p); /* consume IDENT */
            advance(p); /* consume '=' */
            ASTNode *value = parse_expression(p);
            if (!value) { free(name); return NULL; }
            if (!expect(p, TOK_SEMICOLON)) {
                free(name);
                ast_free(value);
                return NULL;
            }
            return ast_assign(name, value);
        }
        /* fall through to expression statement */
    }

    /* expression statement */
    ASTNode *expr = parse_expression(p);
    if (!expr) return NULL;
    if (!expect(p, TOK_SEMICOLON)) { ast_free(expr); return NULL; }
    return ast_expr_stmt(expr);
}

/* block → "{" statement* "}" */
static ASTNode *parse_block(Parser *p) {
    if (!expect(p, TOK_LBRACE)) return NULL;

    int capacity = 8;
    int count = 0;
    ASTNode **stmts = malloc(capacity * sizeof(ASTNode *));

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        ASTNode *stmt = parse_statement(p);
        if (!stmt) {
            for (int i = 0; i < count; i++) ast_free(stmts[i]);
            free(stmts);
            return NULL;
        }
        if (count >= capacity) {
            capacity *= 2;
            stmts = realloc(stmts, capacity * sizeof(ASTNode *));
        }
        stmts[count++] = stmt;
    }

    if (!expect(p, TOK_RBRACE)) {
        for (int i = 0; i < count; i++) ast_free(stmts[i]);
        free(stmts);
        return NULL;
    }

    return ast_block(stmts, count);
}

/* function → "int" "main" "(" ")" block */
static ASTNode *parse_function(Parser *p) {
    if (!expect(p, TOK_KW_INT)) return NULL;

    /* expect identifier "main" */
    if (!check(p, TOK_IDENT)) {
        fprintf(stderr, "error: line %d: expected function name, got '%s'\n",
                p->current.line, token_kind_name(p->current.kind));
        return NULL;
    }
    const char *name = p->current.start;
    int name_len = p->current.length;
    advance(p);

    if (!expect(p, TOK_LPAREN)) return NULL;
    if (!expect(p, TOK_RPAREN)) return NULL;

    ASTNode *body = parse_block(p);
    if (!body) return NULL;

    /* null-terminate the function name */
    char *func_name = malloc(name_len + 1);
    memcpy(func_name, name, name_len);
    func_name[name_len] = '\0';

    return ast_function(func_name, body);
}

void parser_init(Parser *parser, const char *source) {
    lexer_init(&parser->lexer, source);
    parser->current = lexer_next(&parser->lexer);
}

/* program → function */
ASTNode *parser_parse(Parser *parser) {
    return parse_function(parser);
}
