int inc(int x) {
    return x + 1;
}
int main() {
    int a = inc(0);
    int b = inc(a);
    int c = inc(b);
    return c;
}
