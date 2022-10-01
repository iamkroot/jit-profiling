#include "com_craftinginterpreters_lox_Lox.h"
#include <fmt/core.h>

#include <llvm/DebugInfo/DWARF/DWARFContext.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/ExecutionEngine/RuntimeDyld.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/Module.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Object/SymbolSize.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/IR/Verifier.h>
#include <unistd.h>
#include <filesystem>
#include <atomic>

extern "C" int fib(int n) {
    if (n < 2) return n;
    return fib(n - 2) + fib(n - 1);
}

using namespace llvm;

ExecutionEngine* eng;

class MyMCJITMemoryManager : public SectionMemoryManager {
public:
    struct Alloc {
        uint8_t* bytes;
        uintptr_t size;
        llvm::StringRef name;
    };
    static llvm::SmallVector<Alloc, 4> mem_allocs;
    static llvm::SmallVector<Alloc, 8> data_allocs;

    static std::unique_ptr<MyMCJITMemoryManager> Create() {
        return std::make_unique<MyMCJITMemoryManager>();
    }

    MyMCJITMemoryManager() : SectionMemoryManager{} {}

    // Allocate a memory block of (at least) the given size suitable for
    // executable code. The section_id is a unique identifier assigned by the
    // MCJIT engine, and optionally recorded by the memory manager to access a
    // loaded section.
    uint8_t* allocateCodeSection(uintptr_t size, unsigned alignment, unsigned section_id,
                                 llvm::StringRef section_name) override {
        // llvm::outs() << "CODE " << size << " " << alignment << " " << section_id << " "
        //              << section_name << "\n";
        auto* res =
                SectionMemoryManager::allocateCodeSection(size, alignment, section_id, section_name);
        if (section_name == ".text") {
            printf("Mem %lu bytes %s %p\n", size, section_name.data(), res);
        }

        // printf("%p\n", res);
        return res;
    };
};


auto _uniq = std::atomic_int{0};

class MyJitListener : public JITEventListener {
    void notifyObjectLoaded(ObjectKey K, const object::ObjectFile &Obj,
                            const RuntimeDyld::LoadedObjectInfo &L) override {
        outs() << "Loaded key " << K << "\n";
        for (auto section: Obj.sections()) {
            auto addr = L.getSectionLoadAddress(section);
            outs() << "Loaded " << cantFail(section.getName()) << " at " << to_hexString(addr) << "\n";
        }
        llvm::outs().flush();
        using namespace llvm::object;

        OwningBinary<ObjectFile> DebugObjOwner = L.getObjectForDebug(Obj);
        const ObjectFile &DebugObj = *DebugObjOwner.getBinary();

        // auto dctx = DWARFContext::create(DebugObj);
        // dctx->getLineTableForUnit(dctx->getUnitAtIndex(0));
        // dctx->getDIEForOffset(1).getCallerFrame()
        // Get the address of the object image for use as a unique identifier
        std::unique_ptr<DIContext> Context = DWARFContext::create(DebugObj);
        // Context->getLineInfoForAddress()
        // std::fopen(fmt::format("/tmp/{}.lldump", getpid()), );
        // Use symbol info to iterate over functions in the object.

        auto dir = std::filesystem::path(fmt::format("/tmp/.lldump/"));
        std::filesystem::create_directories(dir);
        auto filename = fmt::format("{}.{}.lldump", getpid(), _uniq.fetch_add(1));
        auto file = dir / filename;
        auto ec = std::error_code();

        auto outf = std::fopen(file.c_str(), "w");
        // dump{file, ec, sys::fs::OpenFlags::OF_Text};

        for (const std::pair<SymbolRef, uint64_t> &P: computeSymbolSizes(DebugObj)) {
            SymbolRef Sym = P.first;
            std::string SourceFileName;

            Expected<SymbolRef::Type> SymTypeOrErr = Sym.getType();
            if (!SymTypeOrErr) {
                // There's not much we can with errors here
                consumeError(SymTypeOrErr.takeError());
                continue;
            }
            SymbolRef::Type SymType = *SymTypeOrErr;
            if (SymType != SymbolRef::ST_Function)
                continue;

            Expected<StringRef> Name = Sym.getName();
            if (!Name) {
                consumeError(Name.takeError());
                continue;
            }

            Expected<uint64_t> AddrOrErr = Sym.getAddress();
            if (!AddrOrErr) {
                consumeError(AddrOrErr.takeError());
                continue;
            }
            uint64_t Size = P.second;
            object::SectionedAddress Address;
            Address.Address = *AddrOrErr;

            uint64_t SectionIndex = object::SectionedAddress::UndefSection;
            if (auto SectOrErr = Sym.getSection())
                if (*SectOrErr != Obj.section_end())
                    SectionIndex = SectOrErr.get()->getIndex();
            fmt::print("sym {} size {} addr {:x} section {}\n", *Name, Size, Address.Address, SectionIndex);
            fmt::print(outf, "{} {} {}\n", *Name, Size, Address.Address);
            Address.SectionIndex = SectionIndex;

            auto spec = DILineInfoSpecifier{DILineInfoSpecifier::FileLineInfoKind::BaseNameOnly,
                                            DILineInfoSpecifier::FunctionNameKind::ShortName};
            // According to spec debugging info has to come before loading the
            // corresonding code load.
            DILineInfoTable Lines = Context->getLineInfoForAddressRange(
                    {*AddrOrErr, SectionIndex}, Size, spec);
            for (auto &[pc, x]: Lines) {
                fmt::print("addr {:x} info l={} n={} c={}\n", pc, x.Line, x.FunctionName, x.Column);
                auto inliningInfo = Context->getInliningInfoForAddress({pc, SectionIndex}, spec);
                auto numfr = inliningInfo.getNumberOfFrames();
                if (numfr > 1) {
                    for (uint32_t i = 0; i < numfr; ++i) {
                        auto f = inliningInfo.getFrame(i);
                        fmt::print("inline addr {:x} info l={} n={} c={}\n", pc, f.Line, f.FunctionName, f.Column);
                    }
                }
            }
        }
        fmt::print("Dumped to {}\n", file.string());
        std::fflush(stdout);
        if(fclose(outf)) {
            fmt::print(stderr, "Error in close\n");
        };
        fmt::print("Closed\n");
        std::fflush(stdout);
    }
};

[[maybe_unused]] jlong Java_com_craftinginterpreters_lox_Lox_compileJit(JNIEnv*, jclass) {
    fmt::print("Compiling jit!\n");
#ifdef FIB_LL_PATH
    auto input_path = FIB_LL_PATH;
#else
#error "fib ll path not provieded"
#endif
    // auto input_path = "/home/kroot/Programming/langs/prof-jit/cmake-build-debug/CMakeFiles/fib.dir/fib.ll";

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmParser();
    llvm::InitializeNativeTargetAsmPrinter();
    LLVMLinkInMCJIT();

    llvm::LLVMContext ctx{};
    SMDiagnostic err;

    auto buf = cantFail(errorOrToExpected(MemoryBuffer::getFile(input_path)));
    auto mod = parseIR(buf->getMemBufferRef(), err, ctx);
    if (verifyModule(*mod, &llvm::outs())) {
        return 2;
    }

    fmt::print("{}\n", input_path);
    fmt::print("{}\n", mod->getSourceFileName());
    std::fflush(stdout);
    EngineBuilder builder{std::move(mod)};
    std::string err2;
    builder.setErrorStr(&err2);
    builder.setEngineKind(EngineKind::JIT);
    builder.setMCJITMemoryManager(std::make_unique<MyMCJITMemoryManager>());
    eng = builder.create();
    if (!err2.empty()) {
        llvm::errs() << err2;
        return 3;
    }
    auto lis = JITEventListener::createPerfJITEventListener();
    if (lis == nullptr) {
        llvm::errs() << "No perf event list!\n";
    } else {
        eng->RegisterJITEventListener(lis);
        llvm::outs() << "Registered perf event list!\n";
    }
    auto mylis = new MyJitListener();
    eng->RegisterJITEventListener(mylis);

    fmt::print("finalizing\n");
    std::fflush(stdout);

    eng->finalizeObject();
    fmt::print("finalized\n");
    std::fflush(stdout);
    if (eng->hasError()) {
        llvm::errs() << "err " << eng->getErrorMessage() << "\n";
        return 4;
    }
    eng->runStaticConstructorsDestructors(false);
    auto addr = eng->getFunctionAddress("fib");
    fmt::print("generated code at 0x{:x}\n", addr);
    return addr;
}

[[maybe_unused]] jint Java_com_craftinginterpreters_lox_Lox_runJit(JNIEnv*, jclass, jlong addr, jint arg) {
    fmt::print("Running jit! addr=0x{:x} arg={}\n", addr, arg);
    std::fflush(stdout);
    using FibF = int (*)(int);
    auto fibf = reinterpret_cast<FibF>(addr);
    auto val = fibf(arg);
    fmt::print("Got val {}\n", val);
    std::fflush(stdout);
    return val;
}
