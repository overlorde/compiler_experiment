#include <stdarg.h>
#include <stdio.h>

#include "error.h"

static int errors = 0;

static void stderr_sink(void *userdata, const char *msg) {
    (void)userdata;
    fputs(msg, stderr);
    fputc('\n', stderr);
}

static ErrorSink sink = stderr_sink;
static void *sink_userdata = NULL;

void error_set_sink(ErrorSink s, void *userdata) {
    sink = s ? s : stderr_sink;
    sink_userdata = userdata;
}

/* shared formatting for both entry points: build the complete line in a
 * buffer, then hand it to whatever sink is installed. 512 bytes is far
 * beyond any message we emit; longer ones get truncated, not overflowed. */
static void report(int line, const char *fmt, va_list ap) {
    char buf[512];
    int n;

    if (line > 0)
        n = snprintf(buf, sizeof buf, "error: line %d: ", line);
    else
        n = snprintf(buf, sizeof buf, "error: ");
    if (n < 0 || n >= (int)sizeof buf)
        n = (int)sizeof buf - 1;

    vsnprintf(buf + n, sizeof buf - n, fmt, ap);
    sink(sink_userdata, buf);
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
