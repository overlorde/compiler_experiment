#ifndef LEXER_H
#define LEXER_H

/*
 * Lexer — tokenizes C source code.
 *
 * Phase 1 tokens: integer literals, 'int', 'return', 'main',
 *   '(', ')', '{', '}', ';'
 *
 * You'll add more token types as you implement more phases.
 */

typedef enum {
    TOK_EOF,
    TOK_INT_LIT,       /* e.g. 42 */
    TOK_KW_INT,        /* "int" */
    TOK_KW_RETURN,     /* "return" */
    TOK_IDENT,         /* identifiers */
    TOK_LPAREN,        /* ( */
    TOK_RPAREN,        /* ) */
    TOK_LBRACE,        /* { */
    TOK_RBRACE,        /* } */
    TOK_SEMICOLON,     /* ; */
    TOK_PLUS,          /* + */
    TOK_MINUS,         /* - */
    TOK_STAR,          /* * */
    TOK_SLASH,         /* / */
    TOK_PERCENT,       /* % */
    TOK_BANG,          /* ! */
    TOK_TILDE,         /* ~ */
    TOK_LT,           /* < */
    TOK_GT,           /* > */
    TOK_LTE,          /* <= */
    TOK_GTE,          /* >= */
    TOK_EQ,           /* == */
    TOK_NEQ,          /* != */
    TOK_AND,          /* && */
    TOK_OR,           /* || */
    TOK_ASSIGN,       /* = */
    TOK_COMMA,        /* , */
    TOK_KW_IF,        /* "if" */
    TOK_KW_ELSE,      /* "else" */
    TOK_KW_WHILE,     /* "while" */
    TOK_KW_FOR,       /* "for" */
} TokenKind;

typedef struct {
    TokenKind kind;
    const char *start;  /* pointer into source buffer */
    int length;
    int line;
    union {
        int int_value;  /* for TOK_INT_LIT */
    } data;
} Token;

typedef struct {
    const char *source;  /* full source text (null-terminated) */
    const char *current; /* current position in source */
    int line;
} Lexer;

/* Initialize lexer with source text */
void lexer_init(Lexer *lexer, const char *source);

/* Get the next token */
Token lexer_next(Lexer *lexer);

/* Peek at the next token without consuming it */
Token lexer_peek(Lexer *lexer);

/* Return a human-readable name for a token kind */
const char *token_kind_name(TokenKind kind);

#endif

