#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "types.h"

static bool verbose = true;
static int checks = 0;

static void require(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "Unification demo failed: %s\n", message);
        std::exit(1);
    }
}

static const char *result_name(TypeUnifyResult result) {
    switch (result) {
    case TYPE_UNIFY_OK: return "solved";
    case TYPE_UNIFY_MISMATCH: return "rejected: incompatible types";
    case TYPE_UNIFY_OCCURS: return "rejected: recursive type (occurs check)";
    case TYPE_UNIFY_INVALID: return "rejected: missing type";
    }
    return "unknown result";
}

static void constraint(const char *reason, Type *a, Type *b,
                       TypeUnifyResult expected = TYPE_UNIFY_OK) {
    const std::string left = type_name(a);
    const std::string right = type_name(b);
    TypeUnifyResult result = type_unify(a, b);
    if (verbose) {
        std::printf("  %s: %s = %s -> %s\n", reason, left.c_str(), right.c_str(),
                    result_name(result));
        if (result == TYPE_UNIFY_OK)
            std::printf("    now: %s = %s\n", type_name(a), type_name(b));
    }
    require(result == expected, reason);
    if (result == TYPE_UNIFY_OK)
        require(type_equals(a, b), "successful constraints must yield equal types");
}

static void pointer_inference() {
    if (verbose) std::puts("1. Infer a pointer element from a store and check its load");
    Type *element = type_fresh_var();
    Type *pointer = type_ptr_of(element);
    if (verbose) std::printf("  Allocation starts with p : %s\n", type_name(pointer));
    constraint("store(p, 41)", element, type_int());
    constraint("print_int(load(p))", element, type_int());
    require(type_equals(pointer, type_ptr_of(type_int())), "p must become ptr<int>");
    if (verbose) std::printf("  Inferred p : %s; load(p) : %s\n\n",
                             type_name(pointer), type_name(element));
}

static void independent_allocations() {
    if (verbose) std::puts("2. Independent allocations keep independent element types");
    Type *first = type_fresh_var();
    Type *second = type_fresh_var();
    Type *ints = type_ptr_of(first);
    Type *chars = type_ptr_of(second);
    require(ints != chars, "independent unknowns must not share a pointer node");
    constraint("store(numbers, 42)", first, type_int());
    constraint("store(letters, 'A')", second, type_char());
    require(!type_equals(ints, chars), "different allocations may have different types");
    if (verbose) std::printf("  numbers : %s; letters : %s\n\n",
                             type_name(ints), type_name(chars));
}

static void aliases_and_nested_pointers() {
    if (verbose) std::puts("3. Solve aliases through nested pointer types");
    Type *a = type_fresh_var();
    Type *b = type_fresh_var();
    Type *nested = type_ptr_of(type_ptr_of(a));
    constraint("the two unknowns must agree", a, b);
    constraint("nested pointers must agree", nested,
               type_ptr_of(type_ptr_of(type_char())));
    require(type_resolve(a) == type_char() && type_resolve(b) == type_char(),
            "nested constraints must reach both aliases");
    if (verbose) std::printf("  Nested pointer : %s\n\n", type_name(nested));
}

static void incompatible_store() {
    if (verbose) std::puts("4. Reject a conflicting store without losing earlier solutions");
    Type *element = type_fresh_var();
    Type *pointer = type_ptr_of(element);
    constraint("first store contains an integer", element, type_int());
    constraint("second store contains a boolean", element, type_bool(), TYPE_UNIFY_MISMATCH);
    require(type_resolve(element) == type_int(), "a rejected store must preserve int");
    if (verbose) std::printf("  Pointer remains %s\n\n", type_name(pointer));
}

static void recursive_types() {
    if (verbose) std::puts("5. Reject direct and indirect recursive types");
    Type *direct = type_fresh_var();
    constraint("a = ptr<a>", direct, type_ptr_of(direct), TYPE_UNIFY_OCCURS);
    require(direct->link == nullptr, "the direct cycle must not be installed");
    Type *a = type_fresh_var();
    Type *b = type_fresh_var();
    constraint("a = ptr<b>", a, type_ptr_of(b));
    constraint("b = ptr<a>", b, type_ptr_of(a), TYPE_UNIFY_OCCURS);
    require(b->link == nullptr, "the indirect cycle must not be installed");
    constraint("b can still become int", b, type_int());
    if (verbose) std::printf("  After rejecting the cycle, a : %s\n\n", type_name(a));
}

static void equality_and_subtyping() {
    if (verbose) std::puts("6. Keep equality constraints separate from subtype conversions");
    require(type_subtype(type_int(), type_number()), "int remains a subtype of number");
    constraint("int = number", type_int(), type_number(), TYPE_UNIFY_MISMATCH);
    Type *typed = type_ptr_of(type_int());
    require(type_subtype(typed, type_ptr()), "typed pointers may still forget their element");
    constraint("ptr<int> = ptr", typed, type_ptr(), TYPE_UNIFY_MISMATCH);
    if (verbose) std::puts("  The subtype conversions remain available.\n");
}

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--check") == 0) {
        verbose = false;
    } else if (argc != 1) {
        std::fprintf(stderr, "Usage: %s [--check]\n", argv[0]);
        return 1;
    }
    if (verbose) {
        std::puts("ce unification demonstration\n");
        std::puts("This calls the compiler's type engine with constraints for memory operations.");
        std::puts("The current .ce frontend still requires explicit type annotations.\n");
    }
    pointer_inference();
    independent_allocations();
    aliases_and_nested_pointers();
    incompatible_store();
    recursive_types();
    equality_and_subtyping();
    std::printf("Unification demo: 6 scenarios, %d checks passed\n", checks);
}
