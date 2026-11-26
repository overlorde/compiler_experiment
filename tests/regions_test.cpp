#include <cstdio>
#include <cstdlib>
#include "runtime.h"

extern "C" int64_t __ce_region_live_blocks();
extern "C" int64_t __ce_region_live_regions();
extern "C" int64_t __ce_region_peak_blocks();
static int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); \
} } while (0)

int main() {
    CHECK(__ce_region_live_regions() == 0);
    __ce_region_enter(0); // program root
    int64_t root = alloc(8); store(root, 42);
    for (int i = 0; i < 1000; ++i) {
        __ce_region_enter(0); // scalar-returning worker
        int64_t local = alloc(16); store(local, i); store(offset(local, 1), i + 1);
        __ce_region_enter(1); // pointer-returning allocator
        int64_t returned = alloc(8); store(returned, i + 2);
        __ce_region_enter(1); // multiple returning frames share worker's region
        int64_t nested = alloc(8); store(nested, i + 3);
        __ce_region_leave(); __ce_region_leave();
        CHECK(load(returned) == i + 2 && load(nested) == i + 3);
        CHECK(load(offset(local, 1)) == i + 1);
        CHECK(__ce_region_live_blocks() == 4);
        __ce_region_leave();
        CHECK(__ce_region_live_blocks() == 1);
        CHECK(load(root) == 42);
    }
    CHECK(__ce_region_peak_blocks() == 4);
    __ce_region_enter(0);
    __ce_region_enter(2); // heap publication always allocates into root
    int64_t published = alloc(8); store(published, 99);
    __ce_region_leave(); __ce_region_leave();
    CHECK(load(published) == 99);
    CHECK(__ce_region_live_blocks() == 2);
    __ce_region_leave();
    CHECK(__ce_region_live_blocks() == 0 && __ce_region_live_regions() == 0);
    // Independent compilations/entry points can start a new root.
    __ce_region_enter(1); alloc(8); __ce_region_leave();
    CHECK(__ce_region_live_blocks() == 0 && __ce_region_live_regions() == 0);
    CHECK(__ce_region_peak_blocks() == 1);
    std::printf("Region tests: %d checks passed\n", checks);
}
