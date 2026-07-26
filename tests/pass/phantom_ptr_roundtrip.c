int main() {
    ptr<int> p = alloc(8);
    store(p, 41);
    print_int(load(p) + 1);
    return 0;
}
