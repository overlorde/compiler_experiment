#ifndef ERROR_H
#define ERROR_H

/*
 * Central error reporting.
 *
 * Every phase (lexer, parser, typecheck, codegen) reports through here so the
 * message format is consistent and a phase can ask, when it finishes, whether
 * anything went wrong via error_count().
 *
 * Errors are printed to stderr immediately and counted; reporting one does NOT
 * abort. The caller decides when to stop -- typically: finish the phase, then
 * bail if error_count() > 0. That lets a phase report many errors in one run
 * instead of dying on the first.
 */

/*
 * Where formatted error lines go. The default sink writes to stderr, so the
 * CLI behaves as it always has. An embedder (the wasm entry point, a test
 * harness) can install its own sink to capture messages as data instead.
 *
 * The sink receives one complete message per call, already formatted
 * ("error: line N: ..."), without a trailing newline.
 */
typedef void (*ErrorSink)(void *userdata, const char *msg);

/* Install a sink. Passing NULL restores the default stderr sink. */
void error_set_sink(ErrorSink sink, void *userdata);

/* Report an error at a source line. Pass line <= 0 to omit the location. */
void error_at(int line, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Report an error with no source location. */
void error_msg(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/* How many errors have been reported so far. */
int error_count(void);

/* Reset the counter to zero (e.g. between compilations in one process). */
void error_reset(void);

#endif
