int main() {
    int count = 0;
    for (int i = 1; i <= 20; i = i + 1) {
        if (i % 3 == 0) {
            if (i % 5 == 0) {
                count = count + 3;
            } else {
                count = count + 1;
            }
        } else {
            if (i % 5 == 0) {
                count = count + 2;
            }
        }
    }
    return count;
}
