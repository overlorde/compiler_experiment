/* print.cpp — console output primitives.
 *
 * Thin wrappers over stdio; belongs in runtime/ (not stdlib/) because
 * printing needs libc. See runtime.h for the ABI contract.
 */
#include <cstdio>

#include "runtime.h"

extern "C" {

int print_int(int x) {
    std::printf("%d\n", x);
    return 0;
}

int print_bool(int x) {
    std::puts(x ? "true" : "false");
    return 0;
}

int print_char(int x) {
    std::printf("%c", x);
    return 0;
}

} // extern "C"
