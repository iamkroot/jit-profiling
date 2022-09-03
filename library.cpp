#include "com_craftinginterpreters_lox_Lox.h"
#include <fmt/core.h>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/Module.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/IR/Verifier.h>

extern "C" int fib(int n) {
    if (n < 2) return n;
    return fib(n - 2) + fib(n - 1);
}

using namespace llvm;
#ifdef NDEBUG
auto inppath = "/home/kroot/Programming/langs/prof-jit/cmake-build-release/CMakeFiles/fib.dir/fib.c.o";
#else
auto inppath = "/home/kroot/Programming/langs/prof-jit/cmake-build-debug/CMakeFiles/fib.dir/fib.c.o";
#endif
ExecutionEngine* eng;

[[maybe_unused]] jlong Java_com_craftinginterpreters_lox_Lox_compileJit(JNIEnv*, jclass) {
    fmt::print("Compiling jit!\n");
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmParser();
    llvm::InitializeNativeTargetAsmPrinter();

    llvm::LLVMContext ctx{};
    SMDiagnostic err;

    auto buf = cantFail(errorOrToExpected(MemoryBuffer::getFile(inppath)));
    auto mod = parseIR(buf->getMemBufferRef(), err, ctx);
    if (verifyModule(*mod, &llvm::outs())) {
        return 2;
    }

    fmt::print("{}\n", mod->getSourceFileName());
    std::fflush(stdout);
    EngineBuilder builder{std::move(mod)};
    std::string err2;
    builder.setErrorStr(&err2);
    builder.setEngineKind(EngineKind::JIT);
    eng = builder.create();
    if (!err2.empty()) {
        llvm::errs() << err2;
        return 3;
    }

    eng->finalizeObject();
    fmt::print("finalized\n");
    std::fflush(stdout);
    if (eng->hasError()) {
        llvm::errs() << eng->getErrorMessage() << "\n";
        return 4;
    }
    eng->runStaticConstructorsDestructors(false);
    auto addr = eng->getFunctionAddress("fib");
    fmt::print("generated code at 0x{:X}\n", addr);
    return addr;
}

[[maybe_unused]] jint Java_com_craftinginterpreters_lox_Lox_runJit(JNIEnv*, jclass, jlong addr, jint arg) {
    fmt::print("Running jit! addr=0x{:X} arg={}\n", addr, arg);
    std::fflush(stdout);
    using FibF = int (*)(int);
    auto fibf = reinterpret_cast<FibF>(addr);
    auto val = fibf(arg);
    fmt::print("Got val {}\n", val);
    std::fflush(stdout);
    return val;
}
