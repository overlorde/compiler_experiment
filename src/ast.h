#ifndef AST_H
#define AST_H

/*
 * AST node definitions — tagged union style.
 *
 * You'll grow this as you add language features.
 * Phase 1 only needs: integer literal, return statement, function def.
 */

typedef enum {
    NODE_INT_LIT,
    NODE_BINARY_OP,
    NODE_UNARY_OP,
    NODE_RETURN,
    NODE_FUNCTION,
    NODE_VAR_DECL,
    NODE_VAR_REF,
    NODE_ASSIGN,
    NODE_BLOCK,
    NODE_EXPR_STMT,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR,
    NODE_CALL,
    NODE_PROGRAM,
} NodeKind;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_LT, OP_GT, OP_LTE, OP_GTE, OP_EQ, OP_NEQ,
    OP_AND, OP_OR,
} BinaryOp;

typedef enum {
    OP_NEG, OP_NOT, OP_BITNOT,
} UnaryOp;

typedef struct ASTNode ASTNode;

struct ASTNode {
    NodeKind kind;
    union {
        /* NODE_INT_LIT */
        struct { int value; } int_lit;

        /* NODE_BINARY_OP */
        struct { BinaryOp op; ASTNode *left; ASTNode *right; } bin;

        /* NODE_UNARY_OP */
        struct { UnaryOp op; ASTNode *operand; } unary;

        /* NODE_RETURN */
        struct { ASTNode *expr; } ret;

        /* NODE_FUNCTION */
        struct { const char *name; char **params; int param_count; ASTNode *body; } func;

        /* NODE_VAR_DECL — int x (= expr)?; */
        struct { char *name; ASTNode *init; } var_decl;

        /* NODE_VAR_REF — x */
        struct { char *name; } var_ref;

        /* NODE_ASSIGN — x = expr; */
        struct { char *name; ASTNode *value; } assign;

        /* NODE_BLOCK — { stmt* } */
        struct { ASTNode **stmts; int count; } block;

        /* NODE_EXPR_STMT — expr; */
        struct { ASTNode *expr; } expr_stmt;

        /* NODE_IF — if (cond) then_body else else_body */
        struct { ASTNode *cond; ASTNode *then_body; ASTNode *else_body; } if_stmt;

        /* NODE_WHILE — while (cond) body */
        struct { ASTNode *cond; ASTNode *body; } while_stmt;

        /* NODE_FOR — for (init; cond; post) body */
        struct { ASTNode *init; ASTNode *cond; ASTNode *post; ASTNode *body; } for_stmt;

        /* NODE_CALL — func(args) */
        struct { char *name; ASTNode **args; int arg_count; } call;

        /* NODE_PROGRAM — top-level list of functions */
        struct { ASTNode **functions; int count; } program;
    } data;
};

/* Constructors — allocate and return a new node */
ASTNode *ast_int_lit(int value);
ASTNode *ast_binary_op(BinaryOp op, ASTNode *left, ASTNode *right);
ASTNode *ast_unary_op(UnaryOp op, ASTNode *operand);
ASTNode *ast_return(ASTNode *expr);
ASTNode *ast_function(const char *name, char **params, int param_count, ASTNode *body);
ASTNode *ast_var_decl(char *name, ASTNode *init);
ASTNode *ast_var_ref(char *name);
ASTNode *ast_assign(char *name, ASTNode *value);
ASTNode *ast_block(ASTNode **stmts, int count);
ASTNode *ast_expr_stmt(ASTNode *expr);
ASTNode *ast_if(ASTNode *cond, ASTNode *then_body, ASTNode *else_body);
ASTNode *ast_while(ASTNode *cond, ASTNode *body);
ASTNode *ast_for(ASTNode *init, ASTNode *cond, ASTNode *post, ASTNode *body);
ASTNode *ast_call(char *name, ASTNode **args, int arg_count);
ASTNode *ast_program(ASTNode **functions, int count);

/* Debug: print AST to stdout */
void ast_print(ASTNode *node, int indent);

/* Free an AST tree */
void ast_free(ASTNode *node);

#endif
