int main() {
    ptr<int> inner = alloc(8);
    store(inner, 7);

    ptr<ptr<int>> outer = alloc(8);
    store(outer, inner);

    ptr<int> back = load(outer);
    print_int(load(back));
    return 0;
}
