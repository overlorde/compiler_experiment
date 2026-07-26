/* memory.cpp — heap primitives.
 *
 * The only place the language touches the system allocator. Addresses
 * travel through the language as plain i64 words; the casts below are
 * the sole point where a word becomes a real pointer again.
 *
 * These are raw by design: no length tracking, no bounds, no freeing.
 * The arena in stdlib/ builds the safe layer on top, and the typechecker
 * keeps these callable from stdlib only. Nothing frees — reclamation is
 * Phase 4's region analysis, not a runtime concern.
 */
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "runtime.h"

extern "C" {

/* Returns the address of a fresh n-byte block. Never returns null: if the
 * system allocator refuses, abort loudly. That makes non-null a runtime
 * guarantee the language can rely on -- no 0-checks in user code -- until
 * option types (ADTs) can express allocation failure honestly. */
int64_t alloc(int64_t n) {
    void *p = std::malloc(static_cast<size_t>(n));
    if (!p) {
        std::fprintf(stderr, "runtime: alloc(%" PRId64 ") failed\n", n);
        std::abort();
    }
    return reinterpret_cast<int64_t>(p);
}

int64_t load(int64_t p) {
    return *reinterpret_cast<int64_t *>(p);
}

int64_t store(int64_t p, int64_t v) {
    *reinterpret_cast<int64_t *>(p) = v;
    return 0;
}

/* Element-scaled pointer arithmetic: cell i of a block. The *8 lives here
 * -- not in codegen -- so the backend stays type-blind: it sees an ordinary
 * call, never needing to know one operand was an address. */
int64_t offset(int64_t p, int64_t i) {
    return p + 8 * i;
}

} // extern "C"
