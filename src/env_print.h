#ifndef ENV_PRINT_H
#define ENV_PRINT_H

#include <stdio.h>
#include "symtab.h"

/*
 * Debug / inspection layer for the type-checker environments.
 *
 * A SymTab stores its values as void*, so it has no idea how to print them:
 * the var env holds Type*, the func env will hold a function ASTNode*, the
 * op env an overload set, ... So the generic dumper below takes a *renderer*
 * callback that knows how to turn one scope's value into text.
 *
 * Consequence (this is the point of the module): supporting a richer type
 * system later == writing one new renderer. The traversal code never changes,
 * because it only ever touches the SymTab structure, never the value meaning.
 */

/* Render a single stored value (the void* out of a SymEntry) onto `out`.
   Exactly one renderer per environment kind -- see render_type below. */
typedef void (*Renderer)(FILE *out, const void *value);

/* Dump ONE scope: a header line (label + binding count) followed by one
   "name : <rendered value>" line per binding. */
void env_dump(FILE *out, const char *label, const SymTab *env, Renderer render);

/* Dump a whole scope CHAIN: the given scope first, then outward through each
   parent up to the global root. With today's flat var_env this is the same as
   env_dump; it earns its keep once variable scopes actually nest. */
void env_dump_chain(FILE *out, const char *label, const SymTab *leaf, Renderer render);

/* ---- renderers: one per value type an environment can hold ------------ */

/* value is a Type* (used by var_env and type_env). Prints "int", "bool", ... */
void render_type(FILE *out, const void *value);

/* Future, as the type system grows:
 *   void render_func(FILE *out, const void *value);  // value = function ASTNode*
 *   void render_op  (FILE *out, const void *value);  // value = operator overload
 * Declare them here, define them in env_print.c, pass them to env_dump. */

#endif
