# Liveness Analysis

## What you're building

A tool that reads `.ll` files (produced by your compiler) and answers:
at each point in the program, which values are still needed later?

## Before you write any code

1. Compile the test files to IR and look at what you'll be analyzing:

       cd experiments/liveness
       ../../build/cc -emit-llvm test_simple.c /tmp/simple.ll
       cat /tmp/simple.ll

       ../../build/cc -emit-llvm -O1 test_branch.c /tmp/branch.ll
       cat /tmp/branch.ll

       ../../build/cc -emit-llvm -O1 test_loop.c /tmp/loop.ll
       cat /tmp/loop.ll

   test_simple runs without `-O1` (so you see allocas/loads/stores).
   test_branch and test_loop run with `-O1` (SSA form with phi nodes).

2. Draw the CFG for `test_loop.c`'s IR on paper. Four blocks, one back
   edge. Label predecessors and successors for each block.

3. Read **Cooper & Torczon, "Engineering a Compiler", §8.2.1**
   (Live Variables). Read the whole subsection — equations, algorithm,
   and worked example. About 5-6 pages.

   Alternatives if you don't have it:
   - Appel, "Modern Compiler Implementation in C", §10.1
   - Dragon Book (Aho et al), §9.2.5

## Step 1: Implement `compute_use_def()` — PART 4 in liveness.c

Open `liveness.c`, find PART 4. The comments explain the algorithm and
list the exact LLVM-C API functions to call.

**What it does:** For one basic block, figure out which values it
*uses* from outside (use_set) and which values it *defines* (def_set).

**The key insight:** Walk forward through instructions. If an
instruction uses a value that was NOT defined earlier in the same
block, that value must come from outside — it's "upward exposed."

**Test it:**

    make
    ../../build/cc -emit-llvm test_simple.c /tmp/simple.ll
    ./liveness /tmp/simple.ll

You should see USE and DEF sets populated for each block. LiveIn/LiveOut
will be empty (that's Step 2). Verify USE/DEF against the IR by hand.

For `foo` in test_simple.c (unoptimized), entry block should have:
- USE: `%0, %1` (the function params, used by the stores)
- DEF: `%a, %b, %a1, %b2, %add` (every instruction that produces a value)

## Step 2: Implement `compute_liveness()` — PART 5 in liveness.c

Find PART 5 in `liveness.c`. Full algorithm in the comments.

**The two equations** (the entire analysis):

    LiveOut[b] = ∪ LiveIn[s]           for all successors s of b
    LiveIn[b]  = USE[b] ∪ (LiveOut[b] \ DEF[b])

**The key insight:** It's backward. A use in block C makes that value
live through all blocks on the path from its definition to C.

**Test it on all three files:**

    ../../build/cc -emit-llvm test_simple.c /tmp/simple.ll
    ./liveness /tmp/simple.ll

    ../../build/cc -emit-llvm -O1 test_branch.c /tmp/branch.ll
    ./liveness /tmp/branch.ll

    ../../build/cc -emit-llvm -O1 test_loop.c /tmp/loop.ll
    ./liveness /tmp/loop.ll

## What to check in the output

**test_simple.c** (1 block, unoptimized): Warmup. The function params
`%0` and `%1` should be in USE (they come from the caller). All
instruction results are in DEF. Since there's one block, LiveIn = USE.

**test_branch.c** — factorial (3 blocks, with phi):
The function param `%0` is used in `entry` (for the comparison) AND
in `else` (for the multiply). So `%0` must be live exiting `entry`.
The phi in `common.ret` pulls values from both predecessors.

**test_loop.c** (4 blocks, back edge): The star of the show.
The phi values `%i.0` and `%sum.0` are defined in `whilecond` and
used in `whilebody`. But `whilebody` branches BACK to `whilecond`.
The algorithm needs multiple iterations to propagate liveness through
this cycle. Add `printf("iteration %d\n", ...)` to see it converge.

## A note on phi nodes

In optimized IR, you'll see phi instructions like:
`%i.0 = phi i32 [ 0, %entry ], [ %add6, %whilebody ]`

A phi's operands come from specific predecessor blocks. For your first
implementation, just treat all phi operands as regular uses — this is
slightly imprecise but correct enough to learn the algorithm. You can
refine later if you want.

## After it works

Compare unoptimized vs optimized IR for the same program. The
unoptimized form uses alloca/load/store — each load creates a fresh
SSA value with a short live range. After mem2reg, values live longer
(across blocks), which is exactly the information a register allocator
needs.

## Files

    liveness.c       ← PART 4 and PART 5 are yours to implement
    Makefile         ← just run: make
    test_simple.c    ← warmup: one block, straight-line (no -O1)
    test_branch.c    ← factorial: 3 blocks, phi, recursion (-O1)
    test_loop.c      ← while loop: 4 blocks, back edge (-O1)
