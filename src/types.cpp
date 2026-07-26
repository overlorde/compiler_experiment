#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"

/* Primitive types live as singletons -- shared, never allocated or freed. */
static Type t_int  = { TYPE_INT,    NULL, NULL };
static Type t_bool = { TYPE_BOOL,   NULL, NULL };
static Type t_number = { TYPE_NUMBER, NULL, NULL };
static Type t_char = { TYPE_CHAR,   NULL, NULL };
static Type t_ptr  = { TYPE_PTR,    NULL, NULL };   /* bare ptr, no pointee */

Type *type_int(void)  { return &t_int; }
Type *type_bool(void) { return &t_bool; }
Type *type_number(void) { return &t_number; }
Type *type_char(void) { return &t_char; }
Type *type_ptr(void)  { return &t_ptr; }

/* ptr<elem> nodes are interned: one node per distinct pointee, kept forever,
   so the same spelling always yields the same Type* and the display name is
   built exactly once. The table only ever grows -- types are never freed. */
static Type **ptr_cache = NULL;
static int    ptr_cache_count = 0;
static int    ptr_cache_cap = 0;

Type *type_ptr_of(Type *elem) {
    assert(elem);
    for (int i = 0; i < ptr_cache_count; i++)
        if (type_equals(ptr_cache[i]->param, elem))
            return ptr_cache[i];

    if (ptr_cache_count == ptr_cache_cap) {
        ptr_cache_cap = ptr_cache_cap ? ptr_cache_cap * 2 : 8;
        ptr_cache = (Type **)realloc(ptr_cache, ptr_cache_cap * sizeof *ptr_cache);
    }

    Type *t = (Type *)malloc(sizeof *t);
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

int type_equals(const Type *a, const Type *b) {
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    if (a->kind == TYPE_PTR) {
        /* Bare ptr equals only bare ptr; parameterized compares pointees. */
        if (!a->param || !b->param) return a->param == b->param;
        return type_equals(a->param, b->param);
    }
    return 1;   /* primitives: same kind == same type */
}


int type_subtype(const Type *a, const Type *b) {
    assert(a);
    assert(b);

    if(type_equals(a, b)){
        return 1;
    }

    if(a->kind == TYPE_INT && b->kind == TYPE_NUMBER){
        return 1;
    }

    /* Bare ptr converts to and from any ptr<T> -- the void* of the
       language, and the escape hatch that keeps alloc's monomorphic
       `int -> ptr` signature usable until real polymorphism (HM) lands.
       ptr<A> vs ptr<B> with A != B falls through to 0: no variance. */
    if (a->kind == TYPE_PTR && b->kind == TYPE_PTR
        && (!a->param || !b->param)) {
        return 1;
    }
    return 0;
}


const char *type_name(const Type *t) {
    if (!t) return "<unknown>";
    if (t->display) return t->display;
    switch (t->kind) {
        case TYPE_INT:  return "int";
        case TYPE_BOOL: return "bool";
        case TYPE_NUMBER: return "number";
        case TYPE_CHAR: return "char";
        case TYPE_PTR:  return "ptr";
    }
    assert(0 && "type_name: unhandled TypeKind");
    return "<?>";
}
