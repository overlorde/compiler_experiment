
int f(int a){
    int b  = a;
    return b;
}

int main() {
    int x = 10;
    {
    bool ok = x > 5;
    bool z = x < 9;
    }
    return x;
}

