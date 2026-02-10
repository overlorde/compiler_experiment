#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "lexer.h"

void lexer_init(Lexer *lexer, const char *source) {
    lexer->source  = source;
    lexer->current = source;
    lexer->line    = 1;
}

static Token make_token(Lexer *lexer, TokenKind kind, int length) {
    Token tok = {0};
    tok.kind = kind;
    tok.line = lexer->line;
    tok.start = lexer->current;
    tok.length = length;
    lexer->current += length;
    return tok;
}

Token lexer_next(Lexer *lexer) {
    while (isspace(*lexer->current)) {
        if (*lexer->current == '\n') lexer->line++;
        lexer->current++;
    }

    if (*lexer->current == '\0') {
        Token tok = {0};
        tok.kind = TOK_EOF;
        tok.line = lexer->line;
        tok.start = lexer->current;
        tok.length = 0;
        return tok;
    }

    char c = *lexer->current;
    char next = *(lexer->current + 1);

    /* two-character operators (must check before single-char) */
    if (c == '<' && next == '=') return make_token(lexer, TOK_LTE, 2);
    if (c == '>' && next == '=') return make_token(lexer, TOK_GTE, 2);
    if (c == '=' && next == '=') return make_token(lexer, TOK_EQ,  2);
    if (c == '!' && next == '=') return make_token(lexer, TOK_NEQ, 2);
    if (c == '&' && next == '&') return make_token(lexer, TOK_AND, 2);
    if (c == '|' && next == '|') return make_token(lexer, TOK_OR,  2);

    /* single-character tokens */
    switch (c) {
        case '(': return make_token(lexer, TOK_LPAREN,    1);
        case ')': return make_token(lexer, TOK_RPAREN,    1);
        case '{': return make_token(lexer, TOK_LBRACE,    1);
        case '}': return make_token(lexer, TOK_RBRACE,    1);
        case ';': return make_token(lexer, TOK_SEMICOLON, 1);
        case ',': return make_token(lexer, TOK_COMMA,     1);
        case '+': return make_token(lexer, TOK_PLUS,      1);
        case '-': return make_token(lexer, TOK_MINUS,     1);
        case '*': return make_token(lexer, TOK_STAR,      1);
        case '/': return make_token(lexer, TOK_SLASH,     1);
        case '%': return make_token(lexer, TOK_PERCENT,   1);
        case '~': return make_token(lexer, TOK_TILDE,     1);
        case '!': return make_token(lexer, TOK_BANG,      1);
        case '<': return make_token(lexer, TOK_LT,        1);
        case '>': return make_token(lexer, TOK_GT,        1);
        case '=': return make_token(lexer, TOK_ASSIGN,    1);
    }

    /* integer literals */
    if (isdigit(c)) {
        Token tok = {0};
        tok.kind = TOK_INT_LIT;
        tok.line = lexer->line;
        tok.start = lexer->current;
        tok.data.int_value = strtol(tok.start, NULL, 10);
        while (isdigit(*lexer->current)) lexer->current++;
        tok.length = lexer->current - tok.start;
        return tok;
    }

    /* identifiers and keywords */
    if (isalpha(c) || c == '_') {
        Token tok = {0};
        tok.line = lexer->line;
        tok.start = lexer->current;
        while (isalnum(*lexer->current) || *lexer->current == '_')
            lexer->current++;
        tok.length = lexer->current - tok.start;

        if (tok.length == 3 && strncmp(tok.start, "int", 3) == 0)
            tok.kind = TOK_KW_INT;
        else if (tok.length == 6 && strncmp(tok.start, "return", 6) == 0)
            tok.kind = TOK_KW_RETURN;
        else if (tok.length == 2 && strncmp(tok.start, "if", 2) == 0)
            tok.kind = TOK_KW_IF;
        else if (tok.length == 4 && strncmp(tok.start, "else", 4) == 0)
            tok.kind = TOK_KW_ELSE;
        else if (tok.length == 5 && strncmp(tok.start, "while", 5) == 0)
            tok.kind = TOK_KW_WHILE;
        else if (tok.length == 3 && strncmp(tok.start, "for", 3) == 0)
            tok.kind = TOK_KW_FOR;
        else
            tok.kind = TOK_IDENT;
        return tok;
    }

    fprintf(stderr, "error: line %d: unexpected character '%c'\n", lexer->line, c);
    exit(1);
}

Token lexer_peek(Lexer *lexer) {
    /* Save state, call lexer_next, restore state */
    const char *saved = lexer->current;
    int saved_line = lexer->line;

    Token tok = lexer_next(lexer);

    lexer->current = saved;
    lexer->line = saved_line;
    return tok;
}

const char *token_kind_name(TokenKind kind) {
    switch (kind) {
        case TOK_EOF:       return "EOF";
        case TOK_INT_LIT:   return "INT_LIT";
        case TOK_KW_INT:    return "int";
        case TOK_KW_RETURN: return "return";
        case TOK_IDENT:     return "IDENT";
        case TOK_LPAREN:    return "(";
        case TOK_RPAREN:    return ")";
        case TOK_LBRACE:    return "{";
        case TOK_RBRACE:    return "}";
        case TOK_SEMICOLON: return ";";
        case TOK_COMMA:     return ",";
        case TOK_PLUS:      return "+";
        case TOK_MINUS:     return "-";
        case TOK_STAR:      return "*";
        case TOK_SLASH:     return "/";
        case TOK_PERCENT:   return "%";
        case TOK_BANG:      return "!";
        case TOK_TILDE:     return "~";
        case TOK_LT:        return "<";
        case TOK_GT:        return ">";
        case TOK_LTE:       return "<=";
        case TOK_GTE:       return ">=";
        case TOK_EQ:        return "==";
        case TOK_NEQ:       return "!=";
        case TOK_AND:       return "&&";
        case TOK_OR:        return "||";
        case TOK_ASSIGN:    return "=";
        case TOK_KW_IF:     return "if";
        case TOK_KW_ELSE:   return "else";
        case TOK_KW_WHILE:  return "while";
        case TOK_KW_FOR:    return "for";
        default:            return "UNKNOWN";
    }
}

