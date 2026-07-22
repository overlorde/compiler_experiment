int main() {
    int x = 5;
    {
        int x = 99;
    }
    print_int(x); return 0;
}
