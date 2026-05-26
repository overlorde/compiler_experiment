#ifndef SYMTAB_H
#define SYMTAB_H

/*
 * Symbol table with a chain of lexical scopes.
 *
 * Each SymTab is one scope: a flat array of (name -> value) bindings plus a
 * `parent` pointer to the enclosing scope. The global scope is the root, with
 * parent == NULL. A lookup searches the current scope, then walks the parent
 * chain outward to global, so inner scopes shadow outer ones.
 *
 * Design notes:
 *   - Values are void* so one implementation serves any environment (e.g. a
 *     variable env name->Type*, a type env name->TypeDef*). Callers cast.
 *   - Names are borrowed, not copied: the table stores the pointer you pass,
 *     so keep the string alive (AST-owned strings already outlive the table).
 *   - Storage is a fixed-size array (no heap), so a scope can live as a local
 *     variable that dies when its stack frame returns -- matching block scope.
 */

#define SYMTAB_MAX 256

typedef struct {
    const char *name;
    void *value;
} SymEntry;

/* forward-declare so the struct can hold a pointer to its own type */
typedef struct SymTab SymTab;
struct SymTab {
    SymEntry entries[SYMTAB_MAX];
    int count;
    SymTab *parent;        /* enclosing scope; NULL in the global scope */
};

/* Initialize a scope. Pass parent == NULL for the global scope. */
void  symtab_init(SymTab *st, SymTab *parent);

/* Bind name -> value in THIS scope. Appends; re-adding a name shadows it. */
void  symtab_add(SymTab *st, const char *name, void *value);

/* Look up name in this scope, then up the chain to global. NULL if unbound. */
void *symtab_lookup(SymTab *st, const char *name);

/* Like symtab_lookup, but reports which scope held the binding via *found_in
 * (set to NULL if unbound). Test found_in->parent == NULL to ask "is it global?". */
void *symtab_lookup_scope(SymTab *st, const char *name, SymTab **found_in);

/* Look up name in THIS scope only (for redeclaration checks). NULL if absent. */
void *symtab_lookup_local(SymTab *st, const char *name);

/* Follow the parent chain to the root -- the global scope. */
SymTab *symtab_global(SymTab *st);

#endif
