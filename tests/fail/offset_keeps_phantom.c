int main() {
    ptr<int> p = alloc(16);
    ptr<char> q = offset(p, 1);
    return 0;
}
