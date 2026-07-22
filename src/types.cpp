#include <assert.h>

#include "types.h"

/* Primitive types live as singletons -- shared, never allocated or freed. */
static Type t_int  = { TYPE_INT };
static Type t_bool = { TYPE_BOOL };
static Type t_number = { TYPE_NUMBER };
static Type t_char = { TYPE_CHAR };

Type *type_int(void)  { return &t_int; }
Type *type_bool(void) { return &t_bool; }
Type *type_number(void) { return &t_number; }
Type *type_char(void) { return &t_char; }

int type_equals(const Type *a, const Type *b) {
    if (!a || !b) return 0;
    return a->kind == b->kind;   /* primitives: same kind == same type */
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
    return 0;
}


const char *type_name(const Type *t) {
    if (!t) return "<unknown>";
    switch (t->kind) {
        case TYPE_INT:  return "int";
        case TYPE_BOOL: return "bool";
        case TYPE_NUMBER: return "number";
        case TYPE_CHAR: return "char";
    }
    assert(0 && "type_name: unhandled TypeKind");
    return "<?>";
}

