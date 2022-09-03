#include "com_craftinginterpreters_lox_Lox.h"
#include <fmt/core.h>

jlong Java_com_craftinginterpreters_lox_Lox_compileJit(JNIEnv*, jclass) {
    fmt::print("Compiling jit!\n");
    return 123;
}

jint Java_com_craftinginterpreters_lox_Lox_runJit(JNIEnv*, jclass, jlong addr, jint arg) {
    fmt::print("Running jit! {} {}\n", addr, arg);
    return 0;
}
