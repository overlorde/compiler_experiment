#include "types.h"

/* Primitive types live as singletons -- shared, never allocated or freed. */
static Type t_int  = { TYPE_INT };
static Type t_bool = { TYPE_BOOL };

Type *type_int(void)  { return &t_int; }
Type *type_bool(void) { return &t_bool; }

int type_equals(const Type *a, const Type *b) {
    if (!a || !b) return 0;
    return a->kind == b->kind;   /* primitives: same kind == same type */
}

const char *type_name(const Type *t) {
    if (!t) return "<unknown>";
    switch (t->kind) {
        case TYPE_INT:  return "int";
        case TYPE_BOOL: return "bool";
    }
    return "<?>";
}
