#include <stdarg.h>
#include <stdio.h>

#include "error.h"

static int errors = 0;

/* shared formatting for both entry points */
static void report(int line, const char *fmt, va_list ap) {
    fputs("error: ", stderr);
    if (line > 0)
        fprintf(stderr, "line %d: ", line);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    errors++;
}

void error_at(int line, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    report(line, fmt, ap);
    va_end(ap);
}

void error_msg(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    report(0, fmt, ap);   /* no line */
    va_end(ap);
}

int error_count(void) {
    return errors;
}

void error_reset(void) {
    errors = 0;
}
