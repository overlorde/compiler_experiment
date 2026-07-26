int main() {
    ptr<int> a = alloc(40);
    int i = 0;
    while (i < 5) {
        store(offset(a, i), i * i);
        i = i + 1;
    }
    int sum = 0;
    i = 0;
    while (i < 5) {
        sum = sum + load(offset(a, i));
        i = i + 1;
    }
    print_int(sum);
    return 0;
}
