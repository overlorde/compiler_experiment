int main() {
    ptr p = alloc(8);
    store(p, 42);
    print_int(load(p));
    return 0;
}
