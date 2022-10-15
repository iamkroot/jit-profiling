int fib2(int n);

int f2(int n) {
    return n - 1;
}

__attribute__((noinline))
int f1(int n) {
    return f2(n - 3);
}

int f0(int n) {
    return f1(n);
}

int g3(int n) {
    int x = 0;
    for (long i = 0; i < 10000000000L; ++i) {
        x += f1(f1(n));
    }
    return x;
}

int g2(int n){
    if (n % 2) {
        int x = 0;
        for (int i = 0; i < 1000000000; ++i) {
            x += f1(n);
        }
        return x;
    } else {
        return g3(n);
    }
}

__attribute__((noinline))
int g1(int n) {
    return g2(n - 3);
}

int g0(int n) {
    int x = 0;
    for (long i = 0; i < 10000000000L; ++i) {
        x += n % 4 ? g3(n) : g1(n);
    }
    return x;
}

int fibold(int n) {
    return g0(n) + f0(n);
}

int fib(int n) {
    // int x = fib2(n);

    int x = 0;
    return x + g0(n);
    // if (n < 2)
    //     return n;
    // return fib(n-1) + fib(n-2);
}

int sub1(int n) {
    return n-1;
}

int lt2(int n) {
    return n < 2;
}

int fib1(int n) {
    int isless = lt2(n);
    if (isless)
        return n;
    int n2 = sub1(n);
    return fib1(n2) + fib2(sub1(n) - 1);
}

int fib2(int n) {
    if (n < 2)
        return n;
    return fib1(n - 2) + fib2(n - 1);
}
