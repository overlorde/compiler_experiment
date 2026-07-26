/* print.cpp — console output primitives.
 *
 * Thin wrappers over stdio; belongs in runtime/ (not stdlib/) because
 * printing needs libc. See runtime.h for the ABI contract.
 */
#include <cinttypes>
#include <cstdio>

#include "runtime.h"

extern "C" {

int64_t print_int(int64_t x) {
    std::printf("%" PRId64 "\n", x);
    return 0;
}

int64_t print_bool(int64_t x) {
    std::puts(x ? "true" : "false");
    return 0;
}

int64_t print_char(int64_t x) {
    /* %c wants an int; the value is a char code well within range. */
    std::printf("%c", static_cast<int>(x));
    return 0;
}

} // extern "C"
