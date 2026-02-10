int add1(int x) {
    return x + 1;
}
int double_it(int x) {
    return x * 2;
}
int main() {
    return double_it(add1(5));
}
