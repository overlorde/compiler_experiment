#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "types.h"

static int checks = 0;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        std::exit(1); \
    } \
} while (0)

static_assert(std::is_same<decltype(type_resolve(static_cast<Type *>(nullptr))),
                           Type *>::value, "mutable resolution preserves access");
static_assert(std::is_same<decltype(type_resolve(static_cast<const Type *>(nullptr))),
                           const Type *>::value, "const resolution preserves constness");

static void test_resolution() {
    CHECK(type_resolve(static_cast<Type *>(nullptr)) == nullptr);
    CHECK(type_resolve(static_cast<const Type *>(nullptr)) == nullptr);
    Type *primitives[] = {type_int(), type_bool(), type_number(), type_char(), type_ptr()};
    for (Type *t : primitives) {
        CHECK(type_resolve(t) == t);
        CHECK(type_resolve(static_cast<const Type *>(t)) == t);
    }

    Type *a = type_fresh_var();
    Type *b = type_fresh_var();
    CHECK(a != b);
    CHECK(a->var_id != b->var_id);
    CHECK(a->link == nullptr);
    CHECK(type_resolve(a) == a);
    a->link = b;
    CHECK(type_resolve(a) == b);
    CHECK(type_resolve(static_cast<const Type *>(a)) == b);
    b->link = type_int();
    CHECK(type_resolve(a) == type_int());
    CHECK(type_resolve(a) == type_int());
    CHECK(a->link == b);
    CHECK(b->link == type_int());

    Type concrete = *type_int();
    concrete.link = type_bool(); // Only TYPE_VAR links have meaning.
    CHECK(type_resolve(&concrete) == &concrete);

    std::vector<Type> chain(10000);
    for (size_t i = 0; i < chain.size(); ++i) {
        chain[i].kind = TYPE_VAR;
        chain[i].link = i + 1 < chain.size() ? &chain[i + 1] : nullptr;
    }
    CHECK(type_resolve(&chain.front()) == &chain.back());
    chain.back().link = type_char();
    CHECK(type_resolve(&chain.front()) == type_char());
    CHECK(type_resolve(static_cast<const Type *>(&chain.front())) == type_char());
    for (size_t i = 0; i + 1 < chain.size(); ++i)
        CHECK(chain[i].link == &chain[i + 1]);

    Type *elem = type_fresh_var();
    Type *ptr = type_ptr_of(elem);
    Type *alias = type_fresh_var();
    alias->link = ptr;
    CHECK(type_resolve(alias) == ptr);
    CHECK(type_resolve(ptr) == ptr);
    CHECK(type_resolve(ptr->param) == elem);
    elem->link = type_bool();
    CHECK(type_resolve(ptr) == ptr);
    CHECK(ptr->param == elem);
    CHECK(type_resolve(ptr->param) == type_bool());
}

static void test_equality() {
    CHECK(!type_equals(nullptr, nullptr));
    CHECK(!type_equals(nullptr, type_int()));
    CHECK(!type_equals(type_int(), nullptr));
    Type *primitives[] = {type_int(), type_bool(), type_number(), type_char(), type_ptr()};
    for (Type *a : primitives)
        for (Type *b : primitives)
            CHECK(type_equals(a, b) == (a == b));

    Type *a = type_fresh_var();
    Type *b = type_fresh_var();
    CHECK(type_equals(a, a));
    CHECK(!type_equals(a, b));
    CHECK(!type_equals(a, type_int()));
    CHECK(!type_equals(type_int(), a));
    Type same_label = *a;
    CHECK(!type_equals(a, &same_label));
    CHECK(a->link == nullptr && b->link == nullptr);
    a->link = b;
    CHECK(type_equals(a, b));
    CHECK(!type_equals(a, type_int()));
    b->link = type_int();
    Type *c = type_fresh_var();
    c->link = type_int();
    Type *d = type_fresh_var();
    d->link = type_bool();
    CHECK(type_equals(a, c));
    CHECK(type_equals(a, type_int()));
    CHECK(!type_equals(a, d));
    CHECK(!type_equals(a, type_number()));
    CHECK(a->link == b && b->link == type_int());

    Type *pi = type_ptr_of(type_int());
    Type *pb = type_ptr_of(type_bool());
    Type *pa = type_ptr_of(a);
    CHECK(!type_equals(type_ptr(), pi));
    CHECK(!type_equals(pi, type_ptr()));
    CHECK(!type_equals(pi, pb));
    CHECK(type_equals(pa, pi));
    CHECK(type_equals(type_ptr_of(pa), type_ptr_of(pi)));
    CHECK(!type_equals(type_ptr_of(pi), type_ptr_of(pb)));

    Type *unsolved = type_fresh_var();
    Type *types[] = {a, b, c, d, type_int(), type_bool(), pi, pa,
                     type_ptr_of(c), pb, type_ptr(), unsolved};
    std::vector<Type *> links;
    for (Type *t : types) links.push_back(t->link);
    for (Type *x : types) {
        CHECK(type_equals(x, x));
        for (Type *y : types) {
            CHECK(type_equals(x, y) == type_equals(y, x));
            for (Type *z : types)
                if (type_equals(x, y) && type_equals(y, z))
                    CHECK(type_equals(x, z));
        }
    }
    for (size_t i = 0; i < links.size(); ++i)
        CHECK(types[i]->link == links[i]);
}

static void test_pointer_interning() {
    Type *a = type_fresh_var();
    Type *b = type_fresh_var();
    Type *pa = type_ptr_of(a);
    Type *pb = type_ptr_of(b);
    Type *nested = type_ptr_of(pa);
    CHECK(pa != pb);
    CHECK(!type_equals(pa, pb));
    CHECK(type_ptr_of(a) == pa);
    CHECK(type_ptr_of(b) == pb);
    CHECK(type_ptr_of(pa) == nested);
    a->link = b;
    CHECK(type_equals(pa, pb));
    CHECK(type_ptr_of(a) == pa);
    CHECK(type_ptr_of(b) == pb);
    CHECK(pa->param == a && pb->param == b);
    b->link = type_int();
    Type *pi = type_ptr_of(type_int());
    CHECK(type_ptr_of(type_int()) == pi);
    CHECK(type_ptr_of(type_ptr_of(type_int())) == type_ptr_of(pi));
    CHECK(type_ptr_of(a) == pa && type_ptr_of(b) == pb);
    CHECK(pa != pi && pb != pi);
    CHECK(type_equals(pa, pi) && type_equals(pb, pi));
    CHECK(type_equals(nested, type_ptr_of(pi)));
    CHECK(pa->param == a && pb->param == b && nested->param == pa);

    Type *solved = type_fresh_var();
    solved->link = type_int();
    Type *ps = type_ptr_of(solved);
    CHECK(ps != pi);
    CHECK(ps->param == solved);
    CHECK(type_equals(ps, pi));
    CHECK(a->link == b && b->link == type_int());
}

static void test_subtyping() {
    CHECK(type_subtype(type_int(), type_number()));
    CHECK(!type_subtype(type_number(), type_int()));
    CHECK(!type_equals(type_int(), type_number()));
    CHECK(!type_subtype(type_bool(), type_int()));
    CHECK(!type_subtype(type_char(), type_int()));
    Type *pi = type_ptr_of(type_int());
    Type *pn = type_ptr_of(type_number());
    CHECK(type_subtype(pi, type_ptr()));
    CHECK(type_subtype(pn, type_ptr()));
    CHECK(!type_subtype(type_ptr(), pi));
    CHECK(!type_subtype(pi, pn));
    CHECK(!type_subtype(pn, pi));

    Type *a = type_fresh_var();
    Type *b = type_fresh_var();
    CHECK(type_subtype(a, a));
    CHECK(!type_subtype(a, b));
    CHECK(!type_subtype(a, type_int()));
    CHECK(!type_subtype(type_int(), a));
    CHECK(a->link == nullptr && b->link == nullptr);
    a->link = type_int();
    b->link = type_number();
    CHECK(type_subtype(a, b));
    CHECK(!type_subtype(b, a));

    Type *pointer_alias = type_fresh_var();
    Type *bare_alias = type_fresh_var();
    pointer_alias->link = type_ptr_of(a);
    bare_alias->link = type_ptr();
    CHECK(type_subtype(pointer_alias, bare_alias));
    CHECK(type_subtype(pointer_alias, pi));
    CHECK(!type_subtype(bare_alias, pointer_alias));
    CHECK(!type_subtype(pointer_alias, pn));
    CHECK(!type_subtype(pn, pointer_alias));
    CHECK(pointer_alias->link == type_ptr_of(a));
}

static void test_names() {
    CHECK(std::strcmp(type_name(nullptr), "<unknown>") == 0);
    CHECK(std::strcmp(type_name(type_int()), "int") == 0);
    CHECK(std::strcmp(type_name(type_bool()), "bool") == 0);
    CHECK(std::strcmp(type_name(type_number()), "number") == 0);
    CHECK(std::strcmp(type_name(type_char()), "char") == 0);
    CHECK(std::strcmp(type_name(type_ptr()), "ptr") == 0);
    Type *a = type_fresh_var();
    Type *b = type_fresh_var();
    const std::string initial_var_name = "t" + std::to_string(a->var_id);
    CHECK(type_name(a) == initial_var_name);
    Type *pa = type_ptr_of(a);
    Type *nested = type_ptr_of(pa);
    const char *old_name = type_name(pa);
    const char *old_nested = type_name(nested);
    const char *initial_display = pa->display;
    const std::string initial_ptr_name = "ptr<" + initial_var_name + ">";
    CHECK(old_name == initial_ptr_name);
    CHECK(old_nested == "ptr<" + initial_ptr_name + ">");
    a->link = b;
    CHECK(std::strcmp(type_name(a), type_name(b)) == 0);
    b->link = type_char();
    CHECK(std::strcmp(type_name(a), "char") == 0);
    const char *new_name = type_name(pa);
    const char *new_nested = type_name(nested);
    CHECK(std::strcmp(new_name, "ptr<char>") == 0);
    CHECK(std::strcmp(new_nested, "ptr<ptr<char>>") == 0);
    CHECK(old_name == initial_ptr_name);
    CHECK(old_nested == "ptr<" + initial_ptr_name + ">");
    CHECK(pa->display == initial_display);
    CHECK(initial_display == initial_ptr_name);

    Type *alias = type_fresh_var();
    alias->link = nested;
    CHECK(std::strcmp(type_name(alias), "ptr<ptr<char>>") == 0);
    char message[128];
    std::snprintf(message, sizeof message, "%s versus %s", type_name(pa),
                  type_name(type_ptr_of(type_int())));
    CHECK(std::strcmp(message, "ptr<char> versus ptr<int>") == 0);
    for (int i = 0; i < 256; ++i)
        CHECK(type_name(type_ptr_of(type_fresh_var())) != nullptr);
    CHECK(std::strcmp(new_name, "ptr<char>") == 0);
    CHECK(std::strcmp(new_nested, "ptr<ptr<char>>") == 0);
    CHECK(old_name == initial_ptr_name);
    CHECK(a->link == b && b->link == type_char());
}

static void test_unification() {
    CHECK(type_unify(nullptr, nullptr) == TYPE_UNIFY_INVALID);
    Type *untouched = type_fresh_var();
    CHECK(type_unify(untouched, nullptr) == TYPE_UNIFY_INVALID);
    CHECK(type_unify(nullptr, untouched) == TYPE_UNIFY_INVALID);
    CHECK(untouched->link == nullptr);
    CHECK(type_unify(untouched, untouched) == TYPE_UNIFY_OK);
    CHECK(untouched->link == nullptr);

    Type *primitives[] = {type_int(), type_bool(), type_number(), type_char(), type_ptr()};
    for (Type *a : primitives) {
        for (Type *b : primitives) {
            CHECK(type_unify(a, b) == (a == b ? TYPE_UNIFY_OK : TYPE_UNIFY_MISMATCH));
            CHECK(a->link == nullptr && b->link == nullptr);
        }
        Type *left = type_fresh_var();
        Type *right = type_fresh_var();
        CHECK(type_unify(left, a) == TYPE_UNIFY_OK);
        CHECK(type_unify(a, right) == TYPE_UNIFY_OK);
        CHECK(type_resolve(left) == a && type_resolve(right) == a);
        CHECK(type_equals(left, right));
        CHECK(a->link == nullptr);
    }
    Type int_copy = *type_int();
    Type bare_copy = *type_ptr();
    CHECK(type_unify(&int_copy, type_int()) == TYPE_UNIFY_OK);
    CHECK(type_unify(&bare_copy, type_ptr()) == TYPE_UNIFY_OK);

    Type *a = type_fresh_var();
    Type *b = type_fresh_var();
    Type *c = type_fresh_var();
    CHECK(type_unify(a, b) == TYPE_UNIFY_OK);
    CHECK(type_unify(b, c) == TYPE_UNIFY_OK);
    CHECK(type_unify(c, a) == TYPE_UNIFY_OK); // An alias is not a new cycle.
    CHECK(a->link == b && b->link == c && c->link == nullptr);
    CHECK(type_unify(c, type_int()) == TYPE_UNIFY_OK);
    CHECK(type_equals(a, type_int()));
    CHECK(type_unify(type_int(), a) == TYPE_UNIFY_OK);
    CHECK(type_unify(a, type_bool()) == TYPE_UNIFY_MISMATCH);
    CHECK(a->link == b && b->link == c && c->link == type_int());

    Type *elem = type_fresh_var();
    Type *p = type_ptr_of(elem);
    Type *pi = type_ptr_of(type_int());
    CHECK(type_unify(type_ptr_of(p), type_ptr_of(pi)) == TYPE_UNIFY_OK);
    CHECK(type_resolve(elem) == type_int());
    CHECK(type_equals(p, pi));
    CHECK(p->param == elem && type_ptr_of(elem) == p);
    CHECK(type_unify(p, type_ptr_of(type_char())) == TYPE_UNIFY_MISMATCH);
    CHECK(type_resolve(elem) == type_int());

    Type *independent = type_fresh_var();
    Type *pc = type_ptr_of(independent);
    CHECK(type_unify(pc, type_ptr_of(type_char())) == TYPE_UNIFY_OK);
    CHECK(type_resolve(independent) == type_char());
    CHECK(!type_equals(p, pc));

    Type *unknown_elem = type_fresh_var();
    Type *unknown_ptr = type_ptr_of(unknown_elem);
    CHECK(type_unify(type_ptr(), unknown_ptr) == TYPE_UNIFY_MISMATCH);
    CHECK(type_unify(unknown_ptr, type_ptr()) == TYPE_UNIFY_MISMATCH);
    CHECK(unknown_elem->link == nullptr);
    CHECK(type_subtype(unknown_ptr, type_ptr()));
    CHECK(type_unify(type_int(), type_number()) == TYPE_UNIFY_MISMATCH);
    CHECK(type_unify(type_number(), type_int()) == TYPE_UNIFY_MISMATCH);
    CHECK(type_subtype(type_int(), type_number()));
    CHECK(type_unify(pi, type_ptr_of(type_number())) == TYPE_UNIFY_MISMATCH);
}

static void test_occurs_check() {
    Type *a = type_fresh_var();
    Type *pa = type_ptr_of(a);
    CHECK(type_unify(a, pa) == TYPE_UNIFY_OCCURS);
    CHECK(type_unify(pa, a) == TYPE_UNIFY_OCCURS);
    CHECK(type_unify(a, type_ptr_of(pa)) == TYPE_UNIFY_OCCURS);
    CHECK(a->link == nullptr && pa->param == a);

    Type *alias = type_fresh_var();
    CHECK(type_unify(a, alias) == TYPE_UNIFY_OK);
    CHECK(type_unify(alias, pa) == TYPE_UNIFY_OCCURS);
    CHECK(type_unify(type_ptr_of(alias), a) == TYPE_UNIFY_OCCURS);
    CHECK(a->link == alias && alias->link == nullptr);
    CHECK(type_unify(a, type_char()) == TYPE_UNIFY_OK); // Failure left it usable.
    CHECK(type_equals(pa, type_ptr_of(type_char())));

    Type *b = type_fresh_var();
    Type *c = type_fresh_var();
    Type *pb = type_ptr_of(b);
    Type *pc = type_ptr_of(c);
    CHECK(type_unify(b, pc) == TYPE_UNIFY_OK);
    CHECK(type_unify(c, pb) == TYPE_UNIFY_OCCURS);
    CHECK(type_unify(pb, c) == TYPE_UNIFY_OCCURS);
    CHECK(b->link == pc && c->link == nullptr);
    CHECK(type_unify(c, type_bool()) == TYPE_UNIFY_OK);
    CHECK(std::strcmp(type_name(pb), "ptr<ptr<bool>>") == 0);

    // Deep structures should not consume the C++ call stack in the solver.
    Type *deep_elem = type_fresh_var();
    std::vector<Type> left(10000), right(10000);
    for (size_t i = 0; i < left.size(); ++i) {
        left[i].kind = right[i].kind = TYPE_PTR;
        left[i].param = i + 1 < left.size() ? &left[i + 1] : deep_elem;
        right[i].param = i + 1 < right.size() ? &right[i + 1] : type_int();
    }
    CHECK(type_unify(deep_elem, &left.front()) == TYPE_UNIFY_OCCURS);
    CHECK(deep_elem->link == nullptr);
    CHECK(type_unify(&left.front(), &right.front()) == TYPE_UNIFY_OK);
    CHECK(type_resolve(deep_elem) == type_int());
    CHECK(type_unify(&right.front(), &left.front()) == TYPE_UNIFY_OK);
    CHECK(type_unify(deep_elem, type_bool()) == TYPE_UNIFY_MISMATCH);
}

static void test_schemes() {
    Type *a = type_fresh_var();
    Type *captured = type_fresh_var();
    auto scheme = type_generalize({type_ptr_of(a), a, captured}, {captured});
    CHECK(scheme.quantified.size() == 1 && scheme.quantified[0] == a);
    auto ints = type_instantiate(scheme);
    auto chars = type_instantiate(scheme);
    CHECK(ints.size() == 3 && chars.size() == 3);
    CHECK(ints[2] == captured && chars[2] == captured);
    CHECK(ints[1] != chars[1] && ints[1] != a);
    CHECK(type_unify(ints[0], type_ptr_of(type_int())) == TYPE_UNIFY_OK);
    CHECK(type_equals(ints[1], type_int()));
    CHECK(type_unify(chars[1], type_char()) == TYPE_UNIFY_OK);
    CHECK(type_equals(chars[0], type_ptr_of(type_char())));
    CHECK(a->link == nullptr);
    CHECK(type_unify(captured, type_bool()) == TYPE_UNIFY_OK);
    CHECK(type_equals(ints[2], type_bool()) && type_equals(chars[2], type_bool()));

    Type *alias = type_fresh_var();
    CHECK(type_unify(alias, a) == TYPE_UNIFY_OK);
    auto nested = type_generalize({type_ptr_of(type_ptr_of(alias)), a}, {});
    CHECK(nested.quantified.size() == 1);
    auto copy = type_instantiate(nested);
    CHECK(type_unify(copy[1], type_int()) == TYPE_UNIFY_OK);
    CHECK(type_equals(copy[0], type_ptr_of(type_ptr_of(type_int()))));
    CHECK(a->link == nullptr);

    auto mono = type_generalize({a, type_ptr_of(a)}, {type_ptr_of(a)});
    CHECK(mono.quantified.empty());
    auto shared = type_instantiate(mono);
    CHECK(shared[0] == a);
    CHECK(type_unify(shared[0], type_number()) == TYPE_UNIFY_OK);
    CHECK(type_equals(shared[1], type_ptr_of(type_number())));
    auto concrete = type_generalize({type_int(), type_ptr()}, {});
    CHECK(concrete.quantified.empty());
    CHECK(type_instantiate(concrete) == concrete.types);
}

int main() {
    test_resolution();
    test_equality();
    test_pointer_interning();
    test_subtyping();
    test_names();
    test_unification();
    test_occurs_check();
    test_schemes();
    std::printf("Type tests: %d checks passed\n", checks);
    return 0;
}
