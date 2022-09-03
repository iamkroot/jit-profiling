#include "com_craftinginterpreters_lox_Lox.h"
#include <fmt/core.h>

extern "C" int fib(int n) {
    if (n < 2) return n;
    return fib(n - 2) + fib(n - 1);
}

[[maybe_unused]] jlong Java_com_craftinginterpreters_lox_Lox_compileJit(JNIEnv*, jclass) {
    fmt::print("Compiling jit!\n");
    return 123;
}

[[maybe_unused]] jint Java_com_craftinginterpreters_lox_Lox_runJit(JNIEnv*, jclass, jlong addr, jint arg) {
    fmt::print("Running jit! {} {}\n", addr, arg);
    std::fflush(stdout);
    auto val = fib(arg);
    fmt::print("Got val {}\n", val);
    std::fflush(stdout);
    return val;
}
