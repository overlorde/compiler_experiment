int main() {
    int x = 5;
    {
        int x = 99;
    }
    return x;
}
