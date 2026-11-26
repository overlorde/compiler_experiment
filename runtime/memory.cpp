/* memory.cpp — heap primitives.
 *
 * The only place the language touches the system allocator. Addresses
 * travel through the language as plain i64 words; the casts below are
 * the sole point where a word becomes a real pointer again.
 *
 * Blocks belong to compiler-selected regions and are reclaimed together on
 * region exit. Bounds and initialization remain the source program's concern.
 */
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "runtime.h"

namespace {
struct Region {
    std::vector<void *> blocks;
    Region *destination = this;
    ~Region() { for (void *p : blocks) std::free(p); }
};
thread_local std::vector<std::unique_ptr<Region>> regions;
#ifdef CE_REGION_TEST
thread_local int64_t peak_blocks = 0;
#endif
}

extern "C" {

int64_t __ce_region_enter(int64_t policy) {
#ifdef CE_REGION_TEST
    if (regions.empty()) peak_blocks = 0;
#endif
    auto region = std::make_unique<Region>();
    if (!regions.empty()) {
        if (policy == 1) region->destination = regions.back()->destination;
        else if (policy == 2) region->destination = regions.front().get();
    }
    regions.push_back(std::move(region));
    return 0;
}

int64_t __ce_region_leave() {
    if (regions.empty()) std::abort();
    regions.pop_back();
    return 0;
}

#ifdef CE_REGION_TEST
int64_t __ce_region_live_blocks() {
    int64_t blocks = 0;
    for (const auto &region : regions) blocks += region->blocks.size();
    return blocks;
}
int64_t __ce_region_live_regions() { return regions.size(); }
int64_t __ce_region_peak_blocks() { return peak_blocks; }
#endif

/* Returns the address of a fresh n-byte block. Never returns null: if the
 * system allocator refuses, abort loudly. That makes non-null a runtime
 * guarantee the language can rely on -- no 0-checks in user code -- until
 * option types (ADTs) can express allocation failure honestly. */
int64_t alloc(int64_t n) {
    if (regions.empty()) {
        std::fputs("runtime: allocation outside a region\n", stderr);
        std::abort();
    }
    void *p = std::malloc(static_cast<size_t>(n));
    if (!p) {
        std::fprintf(stderr, "runtime: alloc(%" PRId64 ") failed\n", n);
        std::abort();
    }
    regions.back()->destination->blocks.push_back(p);
#ifdef CE_REGION_TEST
    int64_t blocks = __ce_region_live_blocks();
    if (blocks > peak_blocks) peak_blocks = blocks;
#endif
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
