#include <stdio.h>
#include <stdlib.h>

#include "ast.h"

ASTNode *ast_int_lit(int value) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_INT_LIT;
    node->data.int_lit.value = value;
    return node;
}

ASTNode *ast_binary_op(BinaryOp op, ASTNode *left, ASTNode *right) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_BINARY_OP;
    node->data.bin.op = op;
    node->data.bin.left = left;
    node->data.bin.right = right;
    return node;
}

ASTNode *ast_unary_op(UnaryOp op, ASTNode *operand) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_UNARY_OP;
    node->data.unary.op = op;
    node->data.unary.operand = operand;
    return node;
}

ASTNode *ast_return(ASTNode *expr) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_RETURN;
    node->data.ret.expr = expr;
    return node;
}

ASTNode *ast_function(const char *name, char **params, int param_count, ASTNode *body) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_FUNCTION;
    node->data.func.name = name;
    node->data.func.params = params;
    node->data.func.param_count = param_count;
    node->data.func.body = body;
    return node;
}

ASTNode *ast_var_decl(char *name, ASTNode *init) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_VAR_DECL;
    node->data.var_decl.name = name;
    node->data.var_decl.init = init;
    return node;
}

ASTNode *ast_var_ref(char *name) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_VAR_REF;
    node->data.var_ref.name = name;
    return node;
}

ASTNode *ast_assign(char *name, ASTNode *value) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_ASSIGN;
    node->data.assign.name = name;
    node->data.assign.value = value;
    return node;
}

ASTNode *ast_block(ASTNode **stmts, int count) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_BLOCK;
    node->data.block.stmts = stmts;
    node->data.block.count = count;
    return node;
}

ASTNode *ast_expr_stmt(ASTNode *expr) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_EXPR_STMT;
    node->data.expr_stmt.expr = expr;
    return node;
}

ASTNode *ast_call(char *name, ASTNode **args, int arg_count) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_CALL;
    node->data.call.name = name;
    node->data.call.args = args;
    node->data.call.arg_count = arg_count;
    return node;
}

ASTNode *ast_program(ASTNode **functions, int count) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_PROGRAM;
    node->data.program.functions = functions;
    node->data.program.count = count;
    return node;
}

ASTNode *ast_if(ASTNode *cond, ASTNode *then_body, ASTNode *else_body) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_IF;
    node->data.if_stmt.cond = cond;
    node->data.if_stmt.then_body = then_body;
    node->data.if_stmt.else_body = else_body;
    return node;
}

ASTNode *ast_while(ASTNode *cond, ASTNode *body) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_WHILE;
    node->data.while_stmt.cond = cond;
    node->data.while_stmt.body = body;
    return node;
}

ASTNode *ast_for(ASTNode *init, ASTNode *cond, ASTNode *post, ASTNode *body) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) return NULL;
    node->kind = NODE_FOR;
    node->data.for_stmt.init = init;
    node->data.for_stmt.cond = cond;
    node->data.for_stmt.post = post;
    node->data.for_stmt.body = body;
    return node;
}

void ast_print(ASTNode *node, int indent) {
    if (!node) return;

    for (int i = 0; i < indent; i++) printf("  ");

    static const char *bin_op_names[] = {
        [OP_ADD]="+",[OP_SUB]="-",[OP_MUL]="*",[OP_DIV]="/",[OP_MOD]="%",
        [OP_LT]="<",[OP_GT]=">",[OP_LTE]="<=",[OP_GTE]=">=",[OP_EQ]="==",[OP_NEQ]="!=",
        [OP_AND]="&&",[OP_OR]="||",
    };
    static const char *unary_op_names[] = {
        [OP_NEG]="-",[OP_NOT]="!",[OP_BITNOT]="~",
    };

    switch (node->kind) {
        case NODE_INT_LIT:
            printf("IntLit(%d)\n", node->data.int_lit.value);
            break;
        case NODE_BINARY_OP:
            printf("BinOp(%s)\n", bin_op_names[node->data.bin.op]);
            ast_print(node->data.bin.left, indent + 1);
            ast_print(node->data.bin.right, indent + 1);
            break;
        case NODE_UNARY_OP:
            printf("UnaryOp(%s)\n", unary_op_names[node->data.unary.op]);
            ast_print(node->data.unary.operand, indent + 1);
            break;
        case NODE_RETURN:
            printf("Return\n");
            ast_print(node->data.ret.expr, indent + 1);
            break;
        case NODE_FUNCTION:
            printf("Function(%s, %d params)\n", node->data.func.name, node->data.func.param_count);
            ast_print(node->data.func.body, indent + 1);
            break;
        case NODE_CALL:
            printf("Call(%s, %d args)\n", node->data.call.name, node->data.call.arg_count);
            for (int i = 0; i < node->data.call.arg_count; i++)
                ast_print(node->data.call.args[i], indent + 1);
            break;
        case NODE_PROGRAM:
            printf("Program\n");
            for (int i = 0; i < node->data.program.count; i++)
                ast_print(node->data.program.functions[i], indent + 1);
            break;
        case NODE_VAR_DECL:
            printf("VarDecl(%s)\n", node->data.var_decl.name);
            if (node->data.var_decl.init)
                ast_print(node->data.var_decl.init, indent + 1);
            break;
        case NODE_VAR_REF:
            printf("VarRef(%s)\n", node->data.var_ref.name);
            break;
        case NODE_ASSIGN:
            printf("Assign(%s)\n", node->data.assign.name);
            ast_print(node->data.assign.value, indent + 1);
            break;
        case NODE_BLOCK:
            printf("Block\n");
            for (int i = 0; i < node->data.block.count; i++)
                ast_print(node->data.block.stmts[i], indent + 1);
            break;
        case NODE_EXPR_STMT:
            printf("ExprStmt\n");
            ast_print(node->data.expr_stmt.expr, indent + 1);
            break;
        case NODE_IF:
            printf("If\n");
            ast_print(node->data.if_stmt.cond, indent + 1);
            ast_print(node->data.if_stmt.then_body, indent + 1);
            if (node->data.if_stmt.else_body)
                ast_print(node->data.if_stmt.else_body, indent + 1);
            break;
        case NODE_WHILE:
            printf("While\n");
            ast_print(node->data.while_stmt.cond, indent + 1);
            ast_print(node->data.while_stmt.body, indent + 1);
            break;
        case NODE_FOR:
            printf("For\n");
            if (node->data.for_stmt.init)
                ast_print(node->data.for_stmt.init, indent + 1);
            if (node->data.for_stmt.cond)
                ast_print(node->data.for_stmt.cond, indent + 1);
            if (node->data.for_stmt.post)
                ast_print(node->data.for_stmt.post, indent + 1);
            ast_print(node->data.for_stmt.body, indent + 1);
            break;
    }
}

void ast_free(ASTNode *node) {
    if (!node) return;

    switch (node->kind) {
        case NODE_INT_LIT:
            break;
        case NODE_BINARY_OP:
            ast_free(node->data.bin.left);
            ast_free(node->data.bin.right);
            break;
        case NODE_UNARY_OP:
            ast_free(node->data.unary.operand);
            break;
        case NODE_RETURN:
            ast_free(node->data.ret.expr);
            break;
        case NODE_FUNCTION:
            free((char *)node->data.func.name);
            for (int i = 0; i < node->data.func.param_count; i++)
                free(node->data.func.params[i]);
            free(node->data.func.params);
            ast_free(node->data.func.body);
            break;
        case NODE_CALL:
            free(node->data.call.name);
            for (int i = 0; i < node->data.call.arg_count; i++)
                ast_free(node->data.call.args[i]);
            free(node->data.call.args);
            break;
        case NODE_PROGRAM:
            for (int i = 0; i < node->data.program.count; i++)
                ast_free(node->data.program.functions[i]);
            free(node->data.program.functions);
            break;
        case NODE_VAR_DECL:
            free(node->data.var_decl.name);
            ast_free(node->data.var_decl.init);
            break;
        case NODE_VAR_REF:
            free(node->data.var_ref.name);
            break;
        case NODE_ASSIGN:
            free(node->data.assign.name);
            ast_free(node->data.assign.value);
            break;
        case NODE_BLOCK:
            for (int i = 0; i < node->data.block.count; i++)
                ast_free(node->data.block.stmts[i]);
            free(node->data.block.stmts);
            break;
        case NODE_EXPR_STMT:
            ast_free(node->data.expr_stmt.expr);
            break;
        case NODE_IF:
            ast_free(node->data.if_stmt.cond);
            ast_free(node->data.if_stmt.then_body);
            ast_free(node->data.if_stmt.else_body);
            break;
        case NODE_WHILE:
            ast_free(node->data.while_stmt.cond);
            ast_free(node->data.while_stmt.body);
            break;
        case NODE_FOR:
            ast_free(node->data.for_stmt.init);
            ast_free(node->data.for_stmt.cond);
            ast_free(node->data.for_stmt.post);
            ast_free(node->data.for_stmt.body);
            break;
    }

    free(node);
}
