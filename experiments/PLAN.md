# Experiments Plan

## What you already know

Lexer → Parser → AST → LLVM IR → object file. A C subset with ints,
variables, control flow, functions, recursion. LLVM optimization passes
(as a black box). Enough to be dangerous.

## The landscape

Six tracks, roughly ordered by when to start them. Some are parallel,
some depend on each other.

---

### Track A: Look Down — See What Your Code Becomes on Real Hardware

**No code to write.** Just tools + your existing compiler. Do this first.

**A1. Read your own x86**
Use `objdump -d` on the `.o` files your compiler produces. Map each LLVM
IR instruction back to the x86 it became. See what register allocation
actually looks like — your allocas were in memory, LLVM moved them to
`%eax`, `%edi`, etc. Understand the calling convention (System V AMD64):
args in `%edi`, `%esi`, `%edx`..., return in `%eax`.

**A2. Unoptimized vs optimized x86**
Compile the same file with and without `-O1`, then `objdump` both.
The IR diff was abstract — the x86 diff is concrete. Count instructions.
See redundant loads disappear. See how phi nodes became register moves.

**A3. CPU microarchitecture**
Use `perf stat` or `valgrind --tool=cachegrind` on your compiled
programs. See cache misses, branch mispredictions, instructions-per-cycle.
Write a test that's deliberately branch-heavy (nested ifs with
unpredictable conditions) vs one that's straight-line. See the difference
in hardware counters.

**Teaches:** What LLVM is actually doing for you. Why optimization
matters at the machine level. Calling conventions. Register allocation
as a real thing, not just a textbook concept.

---

### Track B: Custom Bytecode + Stack VM

**The centerpiece.** Everything else builds on this.

**B1. Design the bytecode format**
Define a simple stack-based instruction set. Fixed-width or
variable-width opcodes. Start minimal:

```
OP_PUSH_INT <i32>   — push a 32-bit constant
OP_ADD              — pop two, push sum
OP_SUB, OP_MUL, OP_DIV, OP_MOD
OP_NEG, OP_NOT
OP_RET              — pop and return
```

Decide: how are opcodes encoded? 1 byte per opcode? Are immediates
inline? Big-endian or little-endian? Write this down as a spec before
writing code.

**B2. Write the VM (interpreter)**
A C program: load bytecode from a file, run a fetch-decode-execute loop.
A stack (array of int32), instruction pointer, done.

```c
while (1) {
    uint8_t op = code[ip++];
    switch (op) {
        case OP_PUSH_INT: push(read_i32(&code[ip])); ip += 4; break;
        case OP_ADD: { int b = pop(); int a = pop(); push(a + b); } break;
        case OP_RET: return pop();
    }
}
```

This is ~100 lines. Test it by hand-assembling a few bytecode files.

**B3. New compiler backend: AST → bytecode**
Add a `compile_to_bytecode()` function alongside your existing
`codegen()`. Walk the AST, emit bytecode instead of LLVM IR. For
expressions, emit in postfix order (that's what a stack machine wants).
`3 + 4` → `PUSH 3, PUSH 4, ADD`.

**B4. Add control flow**
New opcodes: `OP_JMP <offset>`, `OP_JMP_IF_ZERO <offset>`,
`OP_CMP_LT`, `OP_CMP_EQ`, etc. Now your `if` and `while` compile to
bytecode. You'll face the same forward-reference problem LLVM solves
with basic blocks — you need to patch jump offsets after emitting the
body.

**B5. Add function calls**
New opcodes: `OP_CALL <func_id> <nargs>`, `OP_RETURN`. You need a call
stack (separate from the operand stack, or frames on the same stack).
Each frame has: return address, local variable slots, saved stack base.
This is where you understand what a "stack frame" actually is at the
lowest level.

**B6. Add local variables**
`OP_LOAD_LOCAL <slot>`, `OP_STORE_LOCAL <slot>`. Variables are numbered
slots in the current call frame. This is exactly how the JVM does it.

**Teaches:** How interpreters work. Stack machines. Instruction encoding.
The fetch-decode-execute cycle. Why bytecode exists (portable, compact,
simpler than machine code). Call frames and stack discipline. You'll also
feel why interpreters are slow — every instruction is a switch case, every
operation goes through memory. That motivates Track D (JIT).

---

### Track C: Register VM (after B)

**C1. Redesign as register-based**
Instead of a stack, instructions reference registers (like LLVM IR, like
real CPUs). `ADD r0, r1, r2` instead of `push; push; add`. You need a
register allocator (or just use infinite virtual registers like LLVM and
let them spill to memory).

**C2. Compare**
Run the same program on your stack VM and register VM. Count executed
instructions. The register version will have fewer (no push/pop) but
each instruction is wider (needs register operands). Measure which is
faster. (Spoiler: register VMs usually win because fewer dispatches.)

**Teaches:** The stack-vs-register tradeoff. Why Lua 5 switched from
stack to register. Why Dalvik (Android's old VM) was register-based
while the JVM is stack-based. Why CPUs are register machines.

---

### Track D: Bytecode → LLVM IR (JIT-like, after B)

**D1. Static translator**
Write a program that reads your bytecode and emits LLVM IR (using the
same LLVM-C API you already know). Each bytecode instruction becomes one
or more LLVM IR instructions. The stack becomes explicit: allocas or SSA
values. This is "ahead-of-time" compilation of bytecode.

**D2. Hook up LLVM's JIT**
Use `LLVMCreateMCJITCompilerForModule()` to compile the IR in memory
and get a function pointer. Call it directly. Now you have:
`source → bytecode → LLVM IR → machine code → execute`, all in one
process. That's a JIT compiler.

**D3. Measure the speedup**
Run a tight loop (fibonacci, factorial of large N) interpreted vs JIT'd.
See 10-100x difference. Understand why JIT exists.

**Teaches:** Why JIT compilers exist. How they work. The tradeoff between
compilation time and execution speed. How V8, HotSpot, and LuaJIT work
at a conceptual level.

---

### Track E: JVM Bytecode Backend (after B5/B6)

**E1. Understand the .class file format**
Read the JVM spec Chapter 4 (The class File Format). It's a binary
format: magic number, constant pool (strings, class refs, method refs),
methods, attributes. Bytecode lives inside method attributes.

**E2. Emit a minimal .class file**
Write a C program (or a new backend) that produces a valid .class file
with a single `public static int main()` that returns 42. Verify with
`javap -c` and `java`. The hard part is the constant pool bookkeeping,
not the bytecode itself.

**E3. Full backend**
Compile your language to JVM bytecode. JVM is stack-based — almost
identical to your Track B bytecode. The mapping is close to 1:1.
`OP_PUSH_INT` → `bipush`/`sipush`/`ldc`, `OP_ADD` → `iadd`,
`OP_LOAD_LOCAL` → `iload`, etc.

**E4. Run on the JVM**
Your C programs, compiled to .class files, running on `java`. Wild.

**Teaches:** A real-world bytecode format. How Java works under the hood.
The constant pool pattern (used everywhere: ELF, Mach-O, WASM, .NET).
JVM verification (the JVM checks your bytecode is valid before running
— your compiler must emit type-correct code).

---

### Track F: Custom ISA + Emulator (the CPU track)

**F1. Design your instruction set**
A minimal RISC ISA. 16 or 32 registers, fixed-width 32-bit instructions.
Instruction types:
- R-type: `ADD r1, r2, r3` (register-register)
- I-type: `ADDI r1, r2, imm` (register-immediate)
- Load/Store: `LW r1, offset(r2)` / `SW r1, offset(r2)`
- Branch: `BEQ r1, r2, offset`
- Jump: `JAL r1, offset` (jump and link — for function calls)

Look at RISC-V as inspiration (it's clean and well-documented). You're
not copying it, just learning from the design decisions.

**F2. Define the encoding**
Each instruction is 32 bits. Decide which bits are opcode, which are
register fields, which are immediate values. Write it down as a table.
This is hardware design thinking — every bit matters.

**F3. Write the emulator**
A C program: load a binary, run it. Fetch-decode-execute, but now it's
register-based and the encoding is bit-packed.

```c
uint32_t instr = mem[pc];
uint8_t opcode = (instr >> 26) & 0x3F;
uint8_t rd = (instr >> 21) & 0x1F;
// ...
switch (opcode) {
    case OP_ADD: regs[rd] = regs[rs1] + regs[rs2]; break;
    // ...
}
```

**F4. Write an assembler**
Text assembly → binary. `ADD r1, r2, r3` → 32-bit encoded instruction.
This is a mini compiler in itself (lexer + encoder).

**F5. Backend: your bytecode or LLVM IR → your ISA**
Write a code generator that targets your ISA. Now you have the full
pipeline: source → AST → your ISA → runs on your emulator.

**F6. Pipeline simulation (optional, deep)**
Simulate a 5-stage pipeline (fetch, decode, execute, memory, writeback).
See data hazards, stalls, forwarding. This is computer architecture, not
compilers — but it's what the CPU is actually doing with your code.

**Teaches:** How CPUs work. Instruction encoding. The ISA as a contract
between hardware and software. Why RISC vs CISC matters. Register files.
Memory hierarchy (if you simulate it). Pipeline hazards. This is the
deepest "CPU stuff" track.

---

### Track G: Write Your Own LLVM Pass (standalone)

**G1. Analysis pass**
Write a pass that walks the IR and reports stats: instruction count per
basic block, which functions are recursive, which variables are never
read. This is read-only — it doesn't transform anything.

**G2. Transformation pass**
Write a pass that does something simple: constant propagation on a
specific pattern, or strength reduction (`x * 2` → `x << 1`). See your
pass change the IR.

**Note:** LLVM passes use the C++ API, not the C API. This is a step up
in toolchain complexity.

**Teaches:** How optimizations work inside the compiler, not as a black
box. Pattern matching on IR. The pass manager infrastructure.

---

## Dependency graph

```
A (look at x86)     ← do first, no code needed

B (stack VM)         ← the foundation
├── C (register VM)  ← compare with B
├── D (JIT)          ← uses B's bytecode + your LLVM knowledge
├── E (JVM)          ← applies B's concepts to a real target
└── F (custom ISA)   ← can also start independently

G (LLVM passes)      ← independent, do whenever
```

## Suggested order

1. **A1-A2** — takes an afternoon, immediately eye-opening
2. **B1-B6** — the main project, maybe a week
3. **E1-E4** or **D1-D3** — whichever excites you more
4. **F1-F5** — the deep CPU track, a longer project
5. Everything else as interest dictates
