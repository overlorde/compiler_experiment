#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unordered_set>

#include "types.h"

/* Primitive types live as singletons -- shared, never allocated or freed. */
static Type t_int    = { TYPE_INT,    NULL, 0, NULL, NULL };
static Type t_bool   = { TYPE_BOOL,   NULL, 0, NULL, NULL };
static Type t_number = { TYPE_NUMBER, NULL, 0, NULL, NULL };
static Type t_char   = { TYPE_CHAR,   NULL, 0, NULL, NULL };
static Type t_ptr    = { TYPE_PTR,    NULL, 0, NULL, NULL }; /* bare ptr */

Type *type_int(void)  { return &t_int; }
Type *type_bool(void) { return &t_bool; }
Type *type_number(void) { return &t_number; }
Type *type_char(void) { return &t_char; }
Type *type_ptr(void)  { return &t_ptr; }

Type *type_resolve(Type *t) {
    while (t && t->kind == TYPE_VAR && t->link)
        t = t->link;
    return t;
}

const Type *type_resolve(const Type *t) {
    while (t && t->kind == TYPE_VAR && t->link)
        t = t->link;
    return t;
}

/* Constructor identity must stay stable when variables acquire links. Keep
   one node per original element node, rather than using semantic equality.
   The table only ever grows -- types are never freed. */
static Type **ptr_cache = NULL;
static int    ptr_cache_count = 0;
static int    ptr_cache_cap = 0;

Type *type_ptr_of(Type *elem) {
    assert(elem);
    for (int i = 0; i < ptr_cache_count; i++)
        if (ptr_cache[i]->param == elem)
            return ptr_cache[i];

    if (ptr_cache_count == ptr_cache_cap) {
        ptr_cache_cap = ptr_cache_cap ? ptr_cache_cap * 2 : 8;
        ptr_cache = (Type **)realloc(ptr_cache, ptr_cache_cap * sizeof *ptr_cache);
    }

    Type *t = (Type *)calloc(1, sizeof *t);
    t->kind = TYPE_PTR;
    t->param = elem;

    const char *inner = type_name(elem);
    size_t len = strlen(inner) + sizeof "ptr<>";
    char *name = (char *)malloc(len);
    snprintf(name, len, "ptr<%s>", inner);
    t->display = name;

    ptr_cache[ptr_cache_count++] = t;
    return t;
}

static Type **var_registry = NULL;
static int var_registry_count = 0;
static int var_registry_cap = 0;
static int next_var_id = 0;

Type *type_fresh_var(void) {
    if (var_registry_count == var_registry_cap) {
        var_registry_cap = var_registry_cap ? var_registry_cap * 2 : 8;
        var_registry = (Type **)realloc(var_registry, var_registry_cap * sizeof *var_registry);
    }

    Type *t = (Type *)calloc(1, sizeof *t);
    t->kind = TYPE_VAR;
    t->var_id = next_var_id++;

    char *name  = (char *)malloc(16);
    snprintf(name, 16, "t%d", t->var_id);
    t->display = name;

    var_registry[var_registry_count++] = t;
    return t;
}

int type_equals(const Type *a, const Type *b) {
    if (!a || !b) return 0;
    a = type_resolve(a);
    b = type_resolve(b);
    if (a == b) return 1;
    if (a->kind != b->kind) return 0;
    switch (a->kind) {
    case TYPE_VAR:
        return 0; /* distinct unresolved variables are independent unknowns */
    case TYPE_PTR:
        /* Bare ptr equals only bare ptr; parameterized compares pointees. */
        if (!a->param || !b->param) return a->param == b->param;
        return type_equals(a->param, b->param);
    case TYPE_INT: case TYPE_BOOL: case TYPE_NUMBER: case TYPE_CHAR:
        return 1;
    }
    assert(0 && "type_equals: unhandled TypeKind");
    return 0;
}


int type_subtype(const Type *a, const Type *b) {
    assert(a);
    assert(b);
    a = type_resolve(a);
    b = type_resolve(b);

    if (type_equals(a, b)) {
        return 1;
    }

    if (a->kind == TYPE_INT && b->kind == TYPE_NUMBER) {
        return 1;
    }

    /* One direction only: forgetting the pointee is safe (ptr<T> <: ptr),
       inventing one is not. The old both-ways rule was a laundering hole --
       ptr<int> -> ptr -> ptr<char> in two legal assignments. Typed pointers
       now enter the world solely through alloc<T>. ptr<A> vs ptr<B> with
       A != B falls through to 0: no variance. */
    if (a->kind == TYPE_PTR && b->kind == TYPE_PTR
        && a->param && !b->param) {
        return 1;
    }
    return 0;
}


const char *type_name(const Type *t) {
    t = type_resolve(t);
    if (!t) return "<unknown>";
    if (t->kind == TYPE_PTR && t->param) {
        /* Never reuse the construction-time spelling: element variables may
           now be solved. Interned strings survive rehashes and later renders. */
        static std::unordered_set<std::string> names;
        std::string name = std::string("ptr<") + type_name(t->param) + ">";
        return names.insert(name).first->c_str();
    }
    if (t->display) return t->display;
    switch (t->kind) {
        case TYPE_INT:  return "int";
        case TYPE_BOOL: return "bool";
        case TYPE_NUMBER: return "number";
        case TYPE_CHAR: return "char";
        case TYPE_PTR:  return "ptr";
        case TYPE_VAR: return "t?";
    }
    assert(0 && "type_name: unhandled TypeKind");
    return "<?>";
}
