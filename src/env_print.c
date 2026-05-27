#include "env_print.h"
#include "types.h"

/* ---- generic traversal -------------------------------------------------
 * These functions know the SymTab *layout* (entries, count, parent) but never
 * the *meaning* of a value -- that is delegated entirely to the renderer. */

/* How many scopes sit between this one and the global root. The root itself
   (parent == NULL) is depth 0; a function scope chained off it is depth 1,
   a block inside that is depth 2, and so on. */
static int scope_depth(const SymTab *env) {
    int d = 0;
    for (const SymTab *s = env->parent; s != NULL; s = s->parent)
        d++;
    return d;
}

void env_dump(FILE *out, const char *label, const SymTab *env, Renderer render) {
    if (!env) {
        fprintf(out, "[%s] (null)\n", label);
        return;
    }

    /* indent by depth so a scope sits visually under its parent. This is why a
       top-down, once-per-scope walk reads as a tree: each scope already knows how
       deep it is from its parent chain, so the dumper needs no depth argument. */
    int depth = scope_depth(env);
    for (int i = 0; i < depth; i++) fputs("  ", out);

    /* header: depth in the scope chain, binding count, and whether it's root */
    fprintf(out, "[%s] depth %d, %d binding(s)%s\n",
            label, depth, env->count, env->parent ? "" : "  (global)");

    for (int i = 0; i < env->count; i++) {
        for (int j = 0; j < depth; j++) fputs("  ", out);   /* align with the header */
        fprintf(out, "    %-12s : ", env->entries[i].name);
        if (render) render(out, env->entries[i].value);
        else        fprintf(out, "%p", env->entries[i].value);  /* fallback */
        fputc('\n', out);
    }
}

void env_dump_chain(FILE *out, const char *label, const SymTab *leaf, Renderer render) {
    int depth = 0;
    for (const SymTab *s = leaf; s != NULL; s = s->parent, depth++) {
        /* tag the innermost scope with the caller's label; mark each enclosing
           scope with how many hops out it is, so nesting is readable */
        char tag[80];
        if (depth == 0) snprintf(tag, sizeof tag, "%s", label);
        else            snprintf(tag, sizeof tag, "%s (parent +%d)", label, depth);
        env_dump(out, tag, s, render);
    }
}

/* ---- renderers ---------------------------------------------------------
 * One per value type stored in some environment. This is the ONLY place that
 * needs to grow when the type system gets richer. */

void render_type(FILE *out, const void *value) {
    const Type *t = value;
    fprintf(out, "%s", t ? type_name(t) : "<untyped>");
}
