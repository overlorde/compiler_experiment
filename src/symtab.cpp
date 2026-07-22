#include <assert.h>
#include <string.h>

#include "symtab.h"

void symtab_init(SymTab *st, SymTab *parent) {
    assert(st);
    st->count  = 0;
    st->parent = parent;
}

void symtab_add(SymTab *st, const char *name, void *value) {
    assert(st);
    assert(name);
    assert(st->count >= 0 && st->count < SYMTAB_MAX);   /* would silently corrupt past the array */
    st->entries[st->count].name  = name;
    st->entries[st->count].value = value;
    st->count++;
}

void *symtab_lookup_local(SymTab *st, const char *name) {
    assert(st);
    assert(name);
    /* newest-first so the most recent binding in this scope wins (shadowing) */
    for (int i = st->count - 1; i >= 0; i--) {
        if (strcmp(st->entries[i].name, name) == 0)
            return st->entries[i].value;
    }
    return NULL;
}

void *symtab_lookup_scope(SymTab *st, const char *name, SymTab **found_in) {
    assert(st);
    assert(name);
    /* search this scope, then each enclosing scope, out to global;
       report the scope the binding was found in via *found_in */
    for (SymTab *s = st; s != NULL; s = s->parent) {
        for (int i = s->count - 1; i >= 0; i--) {
            if (strcmp(s->entries[i].name, name) == 0) {
                if (found_in) *found_in = s;
                return s->entries[i].value;
            }
        }
    }
    if (found_in) *found_in = NULL;
    return NULL;
}

void *symtab_lookup(SymTab *st, const char *name) {
    /* the chain walk, without caring which scope matched */
    return symtab_lookup_scope(st, name, NULL);
}

SymTab *symtab_global(SymTab *st) {
    assert(st);
    while (st->parent)
        st = st->parent;
    return st;
}
