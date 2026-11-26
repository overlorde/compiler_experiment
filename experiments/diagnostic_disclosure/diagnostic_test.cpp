#include <cassert>
#include <string>
#include "error.h"
#include "types.h"

struct Capture { std::string text, expected, actual, context; int structured = 0; int line = 0; };
static void text(void *p, const char *s) { static_cast<Capture *>(p)->text += s; }
static void diagnostic(void *p, const Diagnostic *d) {
    auto &c = *static_cast<Capture *>(p);
    assert(c.text.empty()); // Callback precedes legacy text formatting/delivery.
    assert(error_count() == 0);
    assert(d->code == DIAGNOSTIC_TYPE_MISMATCH);
    c.expected = type_name(d->expected); c.actual = type_name(d->actual);
    c.context = d->context; c.line = d->line; ++c.structured;
}
int main() {
    Capture c;
    error_reset(); error_set_sink(text, &c); error_set_diagnostic_sink(diagnostic, &c);
    { Type temporary = *type_int(); temporary.display = "private_name";
      error_type_mismatch(7, "argument", &temporary, type_char()); }
    assert(c.expected == "private_name" && c.actual == "char" && c.line == 7);
    assert(c.context == "argument" && c.structured == 1 && error_count() == 1);
    assert(c.text == "error: line 7: argument: expected private_name, got char");
    error_set_diagnostic_sink(nullptr, nullptr);
    error_reset(); c.text.clear();
    error_type_mismatch(7, "argument", type_int(), type_char());
    assert(error_count() == 1 && c.structured == 1);
    assert(c.text == "error: line 7: argument: expected int, got char");
    error_reset(); c.text.clear(); error_at(2, "legacy %s", "message");
    assert(error_count() == 1 && c.text == "error: line 2: legacy message");
    error_set_sink(nullptr, nullptr); error_reset();
}
