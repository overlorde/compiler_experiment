int main() {
    int i = 0;
    int evens = 0;
    while (i < 10) {
        if (i % 2 == 0) {
            evens = evens + 1;
        }
        i = i + 1;
    }
    return evens;
}
