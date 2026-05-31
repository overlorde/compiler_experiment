int add(int a, int b) {
    return a + b;
}

int negate(int x) {
    return -x;
}

bool is_positive(int x) {
    return x > 0;
}

int main() {
    int x = 10;
    int y = add(x, 3);
    bool ok = x > 5;
    bool both = ok && is_positive(y);
    if (both) {
        x = x + 1;
    }
    while (x < 20) {
        x = x + 1;
    }
    for (int i = 0; i < 3; i = i + 1) {
        y = y + i;
    }

    bool b1 = 1 + 2;
    bool b2 = 10;
    int  n1 = ok;
    int  n2 = ok && b1;

    n1 = ok;
    ok = 5;
    nope = 3;

    int bad_add = ok + 1;
    bool bad_cmp = ok < 1;
    bool bad_and = 1 && ok;
    bool bad_eq  = ok == 1;
    int  bad_neg = -ok;
    bool bad_not = !x;

    if (x) { }
    while (x) { }
    for (int j = 0; j; j = j + 1) { }

    int u = unbound_var;
    int c1 = add(1);
    int c2 = add(1, ok);
    int c3 = missing(1);

    return ok;
}
