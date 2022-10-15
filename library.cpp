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
#include <llvm/Support/StringSaver.h>
#include <llvm/IR/Verifier.h>
#include <unistd.h>
#include <filesystem>
#include <atomic>
#include <elf/elf++.hh>
#include <dwarf/dwarf++.hh>

class myloader : public elf::loader {
    const char* buf_start;
    uint64_t buf_size;
public:
    myloader(const char* buf_start, uint64_t buf_size) : buf_start{buf_start}, buf_size{buf_size} {}

    const void* load(off_t offset, size_t size) override {
        fmt::print("Loading elf at offset {} size {}\n", offset, size);
        assert(offset + size <= buf_size);
        return (void*) (buf_start + offset);
    }
};

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

// TODO:
//  1. llvm::StringSaver only uses the default BumpPtr with 4KB init.
//      It is not generic over the bump-ptr implementation.
//      We don't need so much, need to re-implement StringSaver with smaller sizes.
//  2. Should figure out when to clear the allocator. On llvm-module unload?
//      how to know when that happens?
using StringPoolAlloc = llvm::BumpPtrAllocatorImpl<>;

class MyJitListener : public JITEventListener {
    static StringPoolAlloc stringAlloc;
    static StringPoolAlloc btAlloc;

    void notifyObjectLoaded(ObjectKey K, const object::ObjectFile &Obj,
                            const RuntimeDyld::LoadedObjectInfo &L) override {
        outs() << "Loaded key " << K << "\n";
        uintptr_t textAddr = 0;
        for (auto section: Obj.sections()) {
            auto addr = L.getSectionLoadAddress(section);
            auto name = cantFail(section.getName());
            if (name == ".text") {
                textAddr = addr;
            }
            outs() << "Loaded " << name << " at " << to_hexString(addr) << " with " << section.getSize() << "bytes\n";
        }
        llvm::outs().flush();
        using namespace llvm::object;

        OwningBinary<ObjectFile> DebugObjOwner = L.getObjectForDebug(Obj);
        const ObjectFile &DebugObj = *DebugObjOwner.getBinary();
        auto start = DebugObj.getMemoryBufferRef().getBufferStart();
        auto size = DebugObj.getMemoryBufferRef().getBufferSize();

        ::elf::elf myelf{std::make_shared<myloader>(start, size)};
        fmt::print("isval {}\n", myelf.valid());
        auto &hdr = myelf.get_hdr();

        fmt::print("{} {} {}\n", hdr.phnum, hdr.phentsize, hdr.phoff);
        ::dwarf::dwarf mydw{::dwarf::elf::create_loader(myelf)};

        //
        // for(auto& seg:myelf.segments()) {
        //     fmt::print("seg paddr{:x} vaddr{:x}\n", seg.get_hdr().paddr, seg.get_hdr().vaddr);
        // }

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
        auto id = _uniq.fetch_add(1);

        // old dump file
        auto filename = fmt::format("{}.{}.lldump", getpid(), id);
        auto file = dir / filename;

        // File containing backtrace dumps. Each line is
        // pc,btAddr,numFrames
        //  where btAddr is a pointer to array of addresses
        //    this array of addresses is the list of function names corresponding to the pc.
        /*
         * Diagram:
                                             +--------------------------------------------+
                                             |               backtrace pool               |
                                             |                                            |
                                             |                                            |
      .lldump.bt file                        | const char* names[]                        |-__     +------------------+
     +---------------------------+           |        bt1+---------+---------+            |   "__  |   string pool    |
     |pc1 | btAddr1 | numFrames=2|-----------|-----------|funcname1|funcname2|            |      "-|                  |
     +---------------------------+           |           +---------+---------+            |        |                  |
     |pc2 | btAddr2 | numFrames=3|--_____    |                                            |       -|                  |
     +---------------------------+       """-|-_____  bt2+---------+---------+---------+  |     /" | hello\0main\0... |
     |pc3 | btAddr2 | numFrames=3|-----------|-----------|funcnamex|funcname0|funcname2|  |---//---| hashi64\0        |
     +---------------------------+           |           +---------+---------+---------+  | ./     |                  |
     |pc4 | btAddr5 | numFrames=1|-____      |                                            |/"      |                  |
     +---+-----------------------+     ""--__|                                            |        |                  |
     |...|                                   |""--___ bt5+---------+                      |        +------------------+
     +---+                                   |       ""--|funcnamez|                      |
                                             |           +---------+                      |
                                             |                                            |
                                             |                                            |
                                             |                                            |
                                             +--------------------------------------------+

         */
        auto btfilename = fmt::format("{}.{}.lldump.bt", getpid(), id);
        auto btfile = dir / btfilename;

        auto elffilename = fmt::format("{}.{}.o", getpid(), id);
        auto elffile = dir / elffilename;
        // dump elf file to disk
        fmt::print("Elf file {}\n", elffile.string());
        writeToOutput(elffile.string(), [&DebugObj](llvm::raw_ostream &x) {
            auto buf = DebugObj.getMemoryBufferRef().getBuffer();
            x.write(buf.data(), buf.size());
            return Error::success();
        });

        // llvm::StringSet funcNames{};
        llvm::UniqueStringSaver funcNames{stringAlloc};
        llvm::UniqueStringSaver uniqBTs{btAlloc};
        using FrameNamesStack = llvm::SmallVector<uintptr_t, 4>;
        // TODO: We should add a level of indirection to deduplicate the frames stack.
        //  Making this a map from pcAddr to frameStackAddr
        llvm::DenseMap<uintptr_t, FrameNamesStack> backtraces;
        struct SmolBT {
            uintptr_t* start;
            size_t numAddrs;
        };
        llvm::DenseMap<uintptr_t, SmolBT> backtracesSmol;


        // auto outf = std::fopen(file.c_str(), "w");
        auto btoutf = std::fopen(btfile.c_str(), "w");
        // dump the in-memory address of the elf file
        // fmt::print(outf, "NEWFILE {} {} {}\n", K, (uint64_t) start, size);

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
            // fmt::print(outf, "{} {} {}\n", *Name, Size, Address.Address);
            for (auto &cu: mydw.compilation_units()) {
                // fmt::print("file {}\n", cu.get_line_table().begin()->file->path);
                // myelf.get_section(SectionIndex)
                auto addr = Address.Address + 2 - textAddr;
                auto rng = ::dwarf::die_pc_range(cu.root());
                if (!::dwarf::die_pc_range(cu.root()).contains(addr)) {
                    for (auto r: rng) {
                        fmt::print("not found {} {}\n", r.low, r.high);

                    }
                    continue;
                }
                auto &lt = cu.get_line_table();
                auto it = lt.find_address(addr);
                if (it != lt.end()) {
                    fmt::print("[DEBUG] Got LTI {} {:#x}\n", it->get_description(), it->address);
                    fmt::print("line {} \n", it->line);
                }
            }
            Address.SectionIndex = SectionIndex;

            const auto spec = DILineInfoSpecifier{DILineInfoSpecifier::FileLineInfoKind::BaseNameOnly,
                                                  DILineInfoSpecifier::FunctionNameKind::ShortName};
            // According to spec debugging info has to come before loading the
            // corresonding code load.
            // DILineInfoTable Lines = Context->getLineInfoForAddressRange(
            //         {*AddrOrErr, SectionIndex}, Size, spec);
            // FIXME: Instead of doing ++pc, we should be jumping by each instruction offset.
            //  This probably requires disassembling the code.
            for (auto pc = Address.Address; pc < Address.Address + Size; ++pc) {
                // fmt::print("addr {:x} info l={} n={} c={}\n", pc, x.Line, x.FunctionName, x.Column);
                FrameNamesStack bt;
                auto inliningInfo = Context->getInliningInfoForAddress({pc, SectionIndex}, spec);
                auto numfr = inliningInfo.getNumberOfFrames();
                for (uint32_t i = 0; i < numfr; ++i) {
                    auto f = inliningInfo.getFrame(i);
                    auto fnNameAddr = funcNames.save(f.FunctionName);
                    bt.push_back((uintptr_t) fnNameAddr.data());
                    // fmt::print("inline addr {:x} info l={} n={} c={}\n", pc, f.Line, f.FunctionName, f.Column);
                }
                // cast the frameNames (array of addresses) to a string so that it can be uniquified
                llvm::StringRef btStr{(const char*) bt.data(), bt.size() * sizeof(uintptr_t) / sizeof(char)};
                auto btAddr = uniqBTs.save(btStr);
                auto smolbt = SmolBT{(uintptr_t*) btAddr.data(), numfr};
                fmt::print(btoutf, "{} {} {}\n", pc, (uintptr_t) smolbt.start, numfr);
                backtracesSmol.insert({pc, smolbt});
                backtraces.insert({pc, bt});
            }
        }

        fmt::print("Backtraces ({} entries, using {} bytes):\n", backtraces.size(), backtraces.getMemorySize());
        // for (auto &[pc, bt]: backtraces) {
        //     fmt::print("{:x}: [", pc);
        //     for (auto fnNameAddr: bt) {
        //         fmt::print("{},", (const char*) fnNameAddr);
        //     }
        //     fmt::print("\b]\n");
        // }
        fmt::print("BacktracesSmol ({} entries, using {} bytes):\n", backtracesSmol.size(),
                   backtracesSmol.getMemorySize());
        for (auto &[pc, btAddr]: backtracesSmol) {
            fmt::print("{:x}: [", pc);
            for (int i = 0; i < btAddr.numAddrs; ++i) {
                fmt::print("{},", (const char*) btAddr.start[i]);
            }
            fmt::print("\b]\n");
        }
        fmt::print("Strings pool stored {} bytes, total {} bytes\n", stringAlloc.getBytesAllocated(),
                   stringAlloc.getTotalMemory());
        fmt::print("Frames pool stored {} bytes, total {} bytes\n", btAlloc.getBytesAllocated(),
                   btAlloc.getTotalMemory());
        fmt::print("Dumped to {}\n", file.string());
        std::fflush(stdout);
        // if (fclose(outf)) {
        //     fmt::print(stderr, "Error in close\n");
        // };
        if (fclose(btoutf)) {
            fmt::print(stderr, "Error in closing btfile\n");
        };
        fmt::print("Closed\n");
        std::fflush(stdout);
    }
};

StringPoolAlloc MyJitListener::stringAlloc;
StringPoolAlloc MyJitListener::btAlloc;

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
