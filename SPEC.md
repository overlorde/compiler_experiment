# cc — Toy C Compiler Spec

A minimal C compiler targeting LLVM IR. Compiles a subset of C sufficient to
run real algorithms, then uses LLVM's infrastructure to explore optimization
and analysis passes.

## Language Subset

**Types:** `int` only (32-bit signed). No float, char, void, struct, enum, union, typedef.

**What's in:**
- Integer literals, arithmetic, comparisons, logical/bitwise ops
- Local variables, assignment
- `if`/`else`, `while`, `for`
- Braced blocks, nested scoping
- Functions (int params, int return), recursion
- `printf` via extern declaration (for output)

**What's out:**
- Preprocessor (`#include`, `#define`)
- Pointers, arrays, strings (maybe later)
- Global variables
- Switch, do-while, break, continue, goto
- Multiple types, casts, sizeof

---

## Tokens

```
Literals        TOK_INT_LIT          42

Keywords        TOK_KW_INT           int
                TOK_KW_RETURN        return
                TOK_KW_IF            if
                TOK_KW_ELSE          else
                TOK_KW_WHILE         while
                TOK_KW_FOR           for

Identifiers     TOK_IDENT            foo, main, x

Arithmetic      TOK_PLUS             +
                TOK_MINUS            -
                TOK_STAR             *
                TOK_SLASH            /
                TOK_PERCENT          %

Comparison      TOK_LT               <
                TOK_GT               >
                TOK_LTE              <=
                TOK_GTE              >=
                TOK_EQ               ==
                TOK_NEQ              !=

Logical         TOK_AND              &&
                TOK_OR               ||
                TOK_BANG             !

Bitwise         TOK_TILDE            ~

Assignment      TOK_ASSIGN           =

Punctuation     TOK_LPAREN           (
                TOK_RPAREN           )
                TOK_LBRACE           {
                TOK_RBRACE           }
                TOK_SEMICOLON        ;
                TOK_COMMA            ,

Special         TOK_EOF
```

---

## Grammar

```
program         → function*

function        → "int" IDENT "(" param_list? ")" block
param_list      → "int" IDENT ("," "int" IDENT)*

block           → "{" statement* "}"

statement       → "return" expression ";"
                | "int" IDENT ("=" expression)? ";"
                | "if" "(" expression ")" statement ("else" statement)?
                | "while" "(" expression ")" statement
                | "for" "(" for_init? ";" expression? ";" expression? ")" statement
                | IDENT "=" expression ";"
                | expression ";"
                | block

for_init        → "int" IDENT "=" expression
                | expression

expression      → logical_or

logical_or      → logical_and ("||" logical_and)*
logical_and     → equality ("&&" equality)*
equality        → comparison (("==" | "!=") comparison)*
comparison      → additive (("<" | ">" | "<=" | ">=") additive)*
additive        → multiplicative (("+" | "-") multiplicative)*
multiplicative  → unary (("*" | "/" | "%") unary)*
unary           → ("-" | "!" | "~") unary
                | primary
primary         → INT_LIT
                | IDENT "(" arg_list? ")"       // function call
                | IDENT                          // variable ref
                | "(" expression ")"

arg_list        → expression ("," expression)*
```

---

## AST Nodes

```
NODE_INT_LIT        { int value }
NODE_BINARY_OP      { op, ASTNode *left, *right }
NODE_UNARY_OP       { op, ASTNode *operand }
NODE_VAR_DECL       { char *name, ASTNode *init }          // init is nullable
NODE_VAR_REF        { char *name }
NODE_ASSIGN         { char *name, ASTNode *value }
NODE_RETURN         { ASTNode *expr }
NODE_IF             { ASTNode *cond, *then_body, *else_body }  // else nullable
NODE_WHILE          { ASTNode *cond, *body }
NODE_FOR            { ASTNode *init, *cond, *post, *body }     // all nullable except body
NODE_BLOCK          { ASTNode **stmts, int count }
NODE_CALL           { char *name, ASTNode **args, int arg_count }
NODE_FUNCTION       { char *name, char **params, int param_count, ASTNode *body }
NODE_PROGRAM        { ASTNode **functions, int count }
```

### Operator enums

```
Binary:  OP_ADD  OP_SUB  OP_MUL  OP_DIV  OP_MOD
         OP_LT   OP_GT   OP_LTE  OP_GTE  OP_EQ  OP_NEQ
         OP_AND  OP_OR

Unary:   OP_NEG  OP_NOT  OP_BITNOT
```

---

## Codegen — LLVM-C Mapping

### Expressions

| AST node | LLVM-C call |
|----------|-------------|
| `NODE_INT_LIT` | `LLVMConstInt(i32, value, 0)` |
| `OP_ADD` | `LLVMBuildAdd` |
| `OP_SUB` | `LLVMBuildSub` |
| `OP_MUL` | `LLVMBuildMul` |
| `OP_DIV` | `LLVMBuildSDiv` |
| `OP_MOD` | `LLVMBuildSRem` |
| `OP_LT` | `LLVMBuildICmp(LLVMIntSLT)` → `LLVMBuildZExt` to i32 |
| `OP_GT` | `LLVMBuildICmp(LLVMIntSGT)` → `LLVMBuildZExt` to i32 |
| `OP_LTE` | `LLVMBuildICmp(LLVMIntSLE)` → `LLVMBuildZExt` to i32 |
| `OP_GTE` | `LLVMBuildICmp(LLVMIntSGE)` → `LLVMBuildZExt` to i32 |
| `OP_EQ` | `LLVMBuildICmp(LLVMIntEQ)` → `LLVMBuildZExt` to i32 |
| `OP_NEQ` | `LLVMBuildICmp(LLVMIntNE)` → `LLVMBuildZExt` to i32 |
| `OP_AND` | both sides != 0, `LLVMBuildAnd`, zext |
| `OP_OR` | both sides != 0, `LLVMBuildOr`, zext |
| `OP_NEG` | `LLVMBuildNeg` |
| `OP_NOT` | `LLVMBuildICmp(== 0)` → `LLVMBuildZExt` to i32 |
| `OP_BITNOT` | `LLVMBuildNot` |

### Variables

| Operation | LLVM-C call |
|-----------|-------------|
| Declare | `LLVMBuildAlloca(i32, name)` |
| Assign | `LLVMBuildStore(value, alloca)` |
| Read | `LLVMBuildLoad(alloca, name)` |
| Lookup | Symbol table: `name → LLVMValueRef (alloca)` |

### Control flow

| AST node | LLVM-C pattern |
|----------|----------------|
| `NODE_IF` | `LLVMBuildCondBr(cond, then_bb, else_bb)` + `LLVMBuildBr(merge_bb)` from each branch |
| `NODE_WHILE` | `cond_bb` → `LLVMBuildCondBr(cond, body_bb, after_bb)`, body falls to cond |
| `NODE_FOR` | emit init → `cond_bb` → `LLVMBuildCondBr` → `body_bb` → `post_bb` → back to cond |

### Functions

| Operation | LLVM-C call |
|-----------|-------------|
| Declare | `LLVMFunctionType` + `LLVMAddFunction` |
| Params | `LLVMGetParam(func, i)` → alloca + store |
| Call | `LLVMBuildCall2(func_type, callee, args, nargs)` |
| Extern (printf) | `LLVMAddFunction` with no body — linker resolves it |

---

## Phases

### Phase 1 — Return literals ✅
```c
int main() { return 42; }
```
Lexer, parser, AST, codegen, object file emission, test harness. Done.

### Phase 2 — Expressions
```c
int main() { return (2 + 3) * 4; }
```
Add all operator tokens to lexer. Precedence-climbing parser. `NODE_BINARY_OP`
and `NODE_UNARY_OP` in AST. Extend `codegen_expr` with LLVM arithmetic/compare
instructions.

### Phase 3 — Variables & statements
```c
int main() {
    int x = 10;
    int y = 20;
    x = x + y;
    return x;
}
```
Add `TOK_ASSIGN`, `TOK_COMMA`. Multiple statements in function body →
`NODE_BLOCK`. Variables → alloca/store/load. Symbol table (flat array is fine).

### Phase 4 — Control flow
```c
int main() {
    int sum = 0;
    for (int i = 1; i <= 10; i = i + 1) {
        sum = sum + i;
    }
    return sum;
}
```
Add `if`/`else`/`while`/`for` keywords. Codegen creates basic blocks and
conditional branches. Nested scoping for block-local variables.

### Phase 5 — Functions
```c
int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}
int main() {
    return factorial(5);
}
```
Multiple functions → `NODE_PROGRAM`. Parameters, `NODE_CALL`. Two-pass codegen
(declare all functions, then emit bodies) to handle forward references.

### Phase 6 — LLVM Passes & Analysis
Language is complete. Now explore LLVM's optimization and analysis infrastructure:

**Optimization passes** (via `LLVMPassManager` or the new pass manager API):
- `mem2reg` — promote allocas to SSA registers (huge: cleans up all our alloca/load/store into clean phi nodes)
- Constant folding / propagation
- Dead code elimination
- Function inlining
- Loop-invariant code motion

**Analysis (build our own or use LLVM's):**
- Print LLVM IR before/after passes to see what changed
- CFG visualization (`LLVMViewFunctionCFG` or dump dot files)
- Liveness analysis — walk the IR, compute live-in/live-out sets per basic block
- Reaching definitions
- Dominance tree inspection (`LLVMGetFirstBasicBlock`, walk successors)

**Possible projects:**
- Write a custom LLVM pass in C that walks the IR
- Implement a register allocator visualization (LLVM does it internally, but building one from IR is educational)
- Add `-O0`/`-O1` flags to our compiler that toggle pass pipelines
- Emit LLVM IR (`-emit-llvm` flag) for inspection instead of object code
- Compare our naive codegen output vs. `mem2reg` vs. full `-O2` on the same input

---

## File Layout (final)

```
src/
    main.c          CLI driver (no changes after phase 1)
    lexer.h/c       Tokenizer — grows with each phase
    parser.h/c      Recursive descent — grows with each phase
    ast.h/c         Node types + constructors — grows with each phase
    codegen.h/c     AST → LLVM IR → .o — grows with each phase
tests/
    run_tests.sh    Test harness (no changes)
    phase1/         3 tests  ✅
    phase2/         24 tests (created)
    phase3/         variable tests (TODO)
    phase4/         control flow tests (TODO)
    phase5/         function tests (TODO)
```
