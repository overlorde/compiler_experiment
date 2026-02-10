/*
 * Liveness Analysis Tool
 *
 * Reads a .ll file produced by your compiler and computes which SSA
 * values are live at the entry (LiveIn) and exit (LiveOut) of each
 * basic block.
 *
 * Usage: ./liveness <input.ll>
 *
 * YOUR JOB: implement two functions:
 *   1. compute_use_def()  — PART 4
 *   2. compute_liveness() — PART 5
 *
 * Everything else (sets, loading, printing) is provided.
 *
 * Textbook: Cooper & Torczon, "Engineering a Compiler", §8.2.1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <llvm-c/Core.h>
#include <llvm-c/IRReader.h>

/* ================================================================
 * PART 1: Value Set (provided — don't modify)
 *
 * A simple array-based set of LLVM values.
 * You'll use these functions in your implementation.
 * ================================================================ */

#define MAX_VALUES 512

typedef struct {
    LLVMValueRef items[MAX_VALUES];
    int count;
} ValueSet;

static void set_init(ValueSet *s) {
    s->count = 0;
}

static int set_contains(ValueSet *s, LLVMValueRef val) {
    for (int i = 0; i < s->count; i++)
        if (s->items[i] == val) return 1;
    return 0;
}

static void set_add(ValueSet *s, LLVMValueRef val) {
    if (set_contains(s, val)) return;
    if (s->count >= MAX_VALUES) {
        fprintf(stderr, "set overflow\n");
        exit(1);
    }
    s->items[s->count++] = val;
}

/* dst = dst ∪ src */
static void set_union(ValueSet *dst, ValueSet *src) {
    for (int i = 0; i < src->count; i++)
        set_add(dst, src->items[i]);
}

/* dst = dst \ to_remove  (remove from dst anything in to_remove) */
static void set_subtract(ValueSet *dst, ValueSet *to_remove) {
    int j = 0;
    for (int i = 0; i < dst->count; i++) {
        if (!set_contains(to_remove, dst->items[i]))
            dst->items[j++] = dst->items[i];
    }
    dst->count = j;
}

static void set_copy(ValueSet *dst, ValueSet *src) {
    dst->count = src->count;
    memcpy(dst->items, src->items, src->count * sizeof(LLVMValueRef));
}

static int set_equals(ValueSet *a, ValueSet *b) {
    if (a->count != b->count) return 0;
    for (int i = 0; i < a->count; i++)
        if (!set_contains(b, a->items[i])) return 0;
    return 1;
}

static void set_clear(ValueSet *s) {
    s->count = 0;
}

/* ================================================================
 * PART 2: Per-Block Data (provided — don't modify)
 * ================================================================ */

#define MAX_BLOCKS 128

typedef struct {
    LLVMBasicBlockRef block;
    ValueSet use_set;   /* UEVar:   used before defined in this block */
    ValueSet def_set;   /* VarKill: defined (produced) in this block  */
    ValueSet live_in;
    ValueSet live_out;
} BlockInfo;

/* ================================================================
 * PART 3: Helpers (provided — don't modify)
 * ================================================================ */

/*
 * Should we track this value for liveness?
 * YES: instruction results (%add, %cmp, ...) and function args (%0, %1, ...)
 * NO:  constants (42, true), basic block labels, function references
 */
static int is_trackable(LLVMValueRef val) {
    return (LLVMIsAInstruction(val) != NULL) ||
           (LLVMIsAArgument(val) != NULL);
}

/* Look up the BlockInfo for a given basic block. */
static BlockInfo *find_block(BlockInfo *blocks, int n, LLVMBasicBlockRef bb) {
    for (int i = 0; i < n; i++)
        if (blocks[i].block == bb) return &blocks[i];
    return NULL;
}

/* Print a value name: "%add", "%0", etc. */
static void print_value(LLVMValueRef val) {
    const char *name = LLVMGetValueName(val);
    if (name && name[0]) {
        printf("%%%s", name);
        return;
    }
    /* Unnamed value — extract %N from LLVM's printed form */
    char *s = LLVMPrintValueToString(val);
    char *pct = strchr(s, '%');
    if (pct) {
        char *end = pct + 1;
        while (*end && *end != ' ' && *end != ',' && *end != ')') end++;
        printf("%.*s", (int)(end - pct), pct);
    } else {
        printf("<?>");
    }
    LLVMDisposeMessage(s);
}

static void print_set(const char *label, ValueSet *s) {
    printf("    %-10s{ ", label);
    for (int i = 0; i < s->count; i++) {
        if (i > 0) printf(", ");
        print_value(s->items[i]);
    }
    printf(" }\n");
}

/* ================================================================
 * PART 4: Compute USE and DEF — YOU IMPLEMENT THIS
 *
 * Textbook: Cooper & Torczon §8.2.1, Figure 8.14
 *
 * Goal: for one basic block, figure out:
 *   use_set — values this block READS that were defined ELSEWHERE
 *   def_set — values this block DEFINES (instruction results)
 *
 * Algorithm — walk instructions FORWARD through the block:
 *
 *   for each instruction I in block:
 *
 *     (a) Look at what I USES (its operands):
 *         for each operand of I:
 *           - skip it if it's not trackable (constants, labels, etc.)
 *           - skip it if it's already in def_set
 *             (it was produced earlier in THIS block, so not "upward exposed")
 *           - otherwise: add it to use_set
 *             (this block needs this value from outside)
 *
 *     (b) Look at what I DEFINES (its result):
 *         - if I produces a value (its return type is NOT void):
 *           add I itself to def_set
 *         - terminators (br, ret) return void — skip them
 *
 * API functions you need:
 *   LLVMGetFirstInstruction(block)    → first instruction in block
 *   LLVMGetNextInstruction(instr)     → next instruction (NULL at end)
 *   LLVMGetNumOperands(instr)         → how many operands
 *   LLVMGetOperand(instr, i)          → get operand at index i
 *   LLVMTypeOf(val)                   → get the LLVM type of a value
 *   LLVMGetTypeKind(type)             → returns LLVMVoidTypeKind for void
 *
 * ~15 lines of code.
 * ================================================================ */
static void compute_use_def(BlockInfo *info) {
    LLVMBasicBlockRef bb = info->block;
    (void)bb;

    /* TODO: your code here */
}

/* ================================================================
 * PART 5: Iterative Liveness — YOU IMPLEMENT THIS
 *
 * Textbook: Cooper & Torczon §8.2.1, Figure 8.16
 *
 * This is a BACKWARD analysis: information flows from uses back
 * toward definitions, against the direction of execution.
 *
 * The two equations (this is the entire analysis):
 *
 *   LiveOut[b] = ∪ LiveIn[s]           for all successors s of b
 *   LiveIn[b]  = use_set[b] ∪ (LiveOut[b] \ def_set[b])
 *
 * Algorithm — iterate until stable:
 *
 *   (1) All LiveIn and LiveOut sets start empty (already done).
 *
 *   (2) Repeat:
 *         changed = 0
 *
 *         for each block b:
 *
 *           (a) Save a copy of LiveIn[b] (to check for changes later).
 *
 *           (b) Recompute LiveOut[b]:
 *               - Clear LiveOut[b]
 *               - Get the terminator instruction of b
 *               - For each successor s of the terminator:
 *                   LiveOut[b] = LiveOut[b] ∪ LiveIn[s]
 *               (Information flows backward: a successor's needs
 *                become this block's output)
 *
 *           (c) Recompute LiveIn[b]:
 *               - Start with a copy of LiveOut[b]
 *               - Subtract def_set[b]
 *                 (values defined here don't need to come from outside)
 *               - Union with use_set[b]
 *                 (values used here definitely need to be live coming in)
 *               - Store result in LiveIn[b]
 *
 *           (d) If LiveIn[b] differs from the saved copy:
 *               changed = 1
 *
 *       (3) Until changed == 0 (fixed point reached).
 *
 * Why does this terminate? Each iteration can only ADD values to
 * LiveIn/LiveOut sets (never remove). Finite number of values
 * means finite iterations. Usually converges in 2-3 passes.
 *
 * API functions you need:
 *   LLVMGetBasicBlockTerminator(block)   → the br/ret at end of block
 *   LLVMGetNumSuccessors(terminator)     → 0 for ret, 1-2 for br
 *   LLVMGetSuccessor(terminator, i)      → the i-th successor block
 *   find_block(blocks, nblocks, bb)      → find BlockInfo for a block
 *
 * Set functions you need:
 *   set_copy(dst, src)          set_clear(s)
 *   set_union(dst, src)         set_subtract(dst, to_remove)
 *   set_equals(a, b)
 *
 * ~25 lines of code.
 * ================================================================ */
static void compute_liveness(BlockInfo *blocks, int nblocks) {
    (void)blocks;
    (void)nblocks;

    /* TODO: your code here */
}

/* ================================================================
 * PART 6: Main — provided (don't modify)
 * ================================================================ */

static void analyze_function(LLVMValueRef func) {
    const char *fname = LLVMGetValueName(func);
    if (LLVMCountBasicBlocks(func) == 0) return;

    printf("Function: %s\n", fname);

    /* Collect all basic blocks */
    BlockInfo blocks[MAX_BLOCKS];
    int nblocks = 0;
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(func);
         bb; bb = LLVMGetNextBasicBlock(bb)) {
        blocks[nblocks].block = bb;
        set_init(&blocks[nblocks].use_set);
        set_init(&blocks[nblocks].def_set);
        set_init(&blocks[nblocks].live_in);
        set_init(&blocks[nblocks].live_out);
        nblocks++;
    }

    /* Step 1: USE/DEF for each block */
    for (int i = 0; i < nblocks; i++)
        compute_use_def(&blocks[i]);

    /* Step 2: iterative liveness */
    compute_liveness(blocks, nblocks);

    /* Print results */
    for (int i = 0; i < nblocks; i++) {
        printf("  Block: %s\n", LLVMGetBasicBlockName(blocks[i].block));
        print_set("USE:", &blocks[i].use_set);
        print_set("DEF:", &blocks[i].def_set);
        print_set("LiveIn:", &blocks[i].live_in);
        print_set("LiveOut:", &blocks[i].live_out);
        printf("\n");
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input.ll>\n", argv[0]);
        return 1;
    }

    /* Load the .ll file */
    LLVMMemoryBufferRef buf;
    char *error = NULL;
    if (LLVMCreateMemoryBufferWithContentsOfFile(argv[1], &buf, &error)) {
        fprintf(stderr, "error reading %s: %s\n", argv[1], error);
        LLVMDisposeMessage(error);
        return 1;
    }

    LLVMModuleRef mod;
    if (LLVMParseIRInContext(LLVMGetGlobalContext(), buf, &mod, &error)) {
        fprintf(stderr, "error parsing IR: %s\n", error);
        LLVMDisposeMessage(error);
        return 1;
    }

    /* Analyze each function in the module */
    for (LLVMValueRef fn = LLVMGetFirstFunction(mod);
         fn; fn = LLVMGetNextFunction(fn)) {
        analyze_function(fn);
    }

    LLVMDisposeModule(mod);
    return 0;
}
