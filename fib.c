int fib2(int n);

int fib(int n) {
    return fib2(n);
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
