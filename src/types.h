#ifndef TYPES_H
#define TYPES_H

/*
 * Type representation for the type checker.
 *
 * A tagged structure (currently just a kind tag) so it can grow: when
 * structured types arrive -- function types, user-defined ADTs -- add a union
 * of per-kind payloads here, the same way the AST is shaped.
 *
 * Primitive types are singletons (type_int(), type_bool()) -- no allocation,
 * and equality is a kind comparison.
 */

typedef enum {
    TYPE_INT,
    TYPE_BOOL,
    TYPE_NUMBER,
    TYPE_CHAR,
    TYPE_PTR      /* a heap address; erases to the same i64 word as the rest */
    /* grow here: TYPE_FUN, user-defined types, ... */
} TypeKind;

typedef struct Type {
    TypeKind kind;
    struct Type *param;   /* TYPE_PTR: the pointee (phantom -- erased at codegen).
                             NULL means the untyped bare `ptr`. */
    const char *display;  /* cached name for parameterized types ("ptr<int>");
                             NULL for primitives, which have fixed names. */
} Type;

/* Singletons for the primitive types. */
Type *type_int(void);
Type *type_bool(void);
Type *type_number(void);
Type *type_char(void);
Type *type_ptr(void);

/* The parameterized pointer type ptr<elem>. Interned: the same elem always
   returns the same Type*, and the node lives forever like the singletons. */
Type *type_ptr_of(Type *elem);

/* Type equality. For primitives this is just a kind match; it will recurse
   once structured types exist. */
int type_equals(const Type *a, const Type *b);

/*subtyping*/
int  type_subtype(const  Type *a, const Type *b);

/* Human-readable name, for error messages ("int", "bool"). */
const char *type_name(const Type *t);

/*
 * A variable's entry in the type environment (the value stored in the symtab).
 * A struct, not a bare Type*, so it can later carry mutability, ownership
 * state, etc. without reworking the environment.
 */
typedef struct {
    Type *type;
} Binding;

#endif

