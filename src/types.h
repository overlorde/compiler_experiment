#ifndef TYPES_H
#define TYPES_H

#include <vector>

/*
 * Type representation for the type checker.
 *
 * A tagged structure for primitive types, phantom-typed pointers, and
 * inference variables. Future function types and ADTs can extend it.
 *
 * Primitive types are singletons (type_int(), type_bool()) -- no allocation,
 * and equality is a kind comparison.
 */

typedef enum {
    TYPE_INT,
    TYPE_BOOL,
    TYPE_NUMBER,
    TYPE_CHAR,
    TYPE_PTR,       /* a heap address; erases to the same i64 word as the rest */
    TYPE_VAR       /* an unknown type, optionally linked to another type */
    /* grow here: TYPE_FUN, user-defined types, ... */
} TypeKind;

typedef struct Type {
    TypeKind kind;

    struct Type *link; /* TYPE_VAR: NULL when unresolved; otherwise its solution */
    int var_id;       /* TYPE_VAR: diagnostic identity */

    struct Type *param;   /* TYPE_PTR: the pointee (phantom -- erased at codegen).
                             NULL means the untyped bare `ptr`. */
    const char *display;  /* variable name or initial pointer spelling;
                             type_name renders pointers from current elements. */
} Type;

/* Singletons for the primitive types. */
Type *type_int(void);
Type *type_bool(void);
Type *type_number(void);
Type *type_char(void);
Type *type_ptr(void);

Type *type_fresh_var(void);

/* Follow variable links without mutation or path compression. NULL stays
   NULL; concrete types and unresolved variables return themselves. This
   resolves only the root, not pointer elements. Types must be acyclic through
   links and pointer parameters; type_unify preserves that invariant. */
Type *type_resolve(Type *t);
const Type *type_resolve(const Type *t);

/* The parameterized pointer type ptr<elem>. Interned by the original elem
   node's identity, without resolving it. Nodes live forever. Different nodes
   can become structurally equal after links change; compare with type_equals. */
Type *type_ptr_of(Type *elem);

/* Read-only equality after resolution: primitives compare kinds, unresolved
   variables compare node identities, and pointers compare elements recursively.
   Bare ptr equals only bare ptr. Any NULL operand makes equality false.
   Equality neither binds variables nor applies subtype conversions. */
int type_equals(const Type *a, const Type *b);

typedef enum {
    TYPE_UNIFY_OK = 0,
    TYPE_UNIFY_MISMATCH, /* incompatible concrete types or pointer shapes */
    TYPE_UNIFY_OCCURS,   /* binding would create a recursive type */
    TYPE_UNIFY_INVALID,  /* a NULL operand */
} TypeUnifyResult;

/* Solve an equality constraint, linking unresolved variables as necessary.
   Both input graphs must be acyclic. The occurs check prevents new cycles.
   Success makes type_equals(a, b) true; failure leaves all links unchanged.
   Earlier successful calls are not rolled back. This does not apply subtype
   conversions: int/number and ptr<T>/bare ptr are unification mismatches.
   No diagnostics are emitted; callers decide how to report the result. */
TypeUnifyResult type_unify(Type *a, Type *b);

/* Subtyping after resolution; both inputs must be non-NULL. */
int type_subtype(const Type *a, const Type *b);

/* Current name after resolution, including pointer elements. Returned text
   remains valid for the process lifetime, even when other names are rendered. */
const char *type_name(const Type *t);

/* A first-order function signature stores its parameters followed by its
   result. Quantified variables are copied consistently at every use; other
   variables remain shared with the environment. Locals never carry schemes. */
struct TypeScheme {
    std::vector<Type *> types;
    std::vector<Type *> quantified;
};
std::vector<Type *> type_free_vars(const std::vector<Type *> &types);
TypeScheme type_generalize(const std::vector<Type *> &types,
                          const std::vector<Type *> &environment);
std::vector<Type *> type_instantiate(const TypeScheme &scheme);

/*
 * A variable's entry in the type environment (the value stored in the symtab).
 * A struct, not a bare Type*, so it can later carry mutability, ownership
 * state, etc. without reworking the environment.
 */
typedef struct {
    Type *type;
} Binding;

#endif

