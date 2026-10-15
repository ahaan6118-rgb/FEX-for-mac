// Rosetta Killer - macOS x86_64 binary translator PoC
// Runs x86_64 machine code on Apple Silicon via FEXCore's ARM64 JIT

#include "macos_stubs.h"
#include "elf_loader.h"
#include "pe_loader.h"
#include "dll_loader.h"
#include "win32_stubs.h"
#include "test_elfs.h"
#include "test_pes.h"

#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/Context.h>
#include <FEXCore/Core/CoreState.h>
#include <FEXCore/Core/HostFeatures.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/Utils/TypeDefines.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <pthread.h>
#include <unistd.h>

using FEXCore::Core::CPUState;
using FEXCore::Core::InternalThreadState;

static void LogHandler(LogMan::DebugLevels Level, const char* Message) {
  fprintf(stderr, "[FEX %s] %s\n", LogMan::DebugLevelStr(Level), Message);
}

static void AssertHandler(const char* Message) {
  fprintf(stderr, "[FEX ASSERT] %s\n", Message);
  abort();
}

// --- Minimal Windows TEB/PEB for PE execution ---
// The MinGW CRT reads gs:0x30 (TEB self-pointer), gs:0x08 (stack base), gs:0x60 (PEB), etc.
struct alignas(4096) FakeTEB {
  uint64_t ExceptionList;       // 0x00 — SEH chain, -1 = end
  uint64_t StackBase;           // 0x08
  uint64_t StackLimit;          // 0x10
  uint64_t SubSystemTib;        // 0x18
  uint64_t FiberData;           // 0x20
  uint64_t ArbitraryUserPtr;    // 0x28
  uint64_t Self;                // 0x30 — pointer to this TEB
  uint64_t EnvironmentPointer;  // 0x38
  uint64_t ClientId_Process;    // 0x40
  uint64_t ClientId_Thread;     // 0x48
  uint64_t ActiveRpcHandle;     // 0x50
  uint64_t ThreadLocalStorage;  // 0x58
  uint64_t PEB;                 // 0x60 — pointer to PEB
  uint8_t  pad[4096 - 0x68];
};

struct alignas(4096) FakePEB {
  uint8_t  InheritedAddressSpace; // 0x00
  uint8_t  ReadImageFileExecOptions;
  uint8_t  BeingDebugged;         // 0x02
  uint8_t  pad0[5];
  uint64_t Mutant;                // 0x08
  uint64_t ImageBaseAddress;      // 0x10
  uint64_t Ldr;                   // 0x18
  uint64_t ProcessParameters;     // 0x20
  uint8_t  pad1[4096 - 0x28];
};

static FakeTEB* g_TEB = nullptr;
static FakePEB* g_PEB = nullptr;

static void SetupFakeTEB(uint64_t stack_base, uint64_t stack_limit, uint64_t image_base) {
  if (!g_TEB) {
    g_TEB = static_cast<FakeTEB*>(mmap(nullptr, sizeof(FakeTEB),
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    g_PEB = static_cast<FakePEB*>(mmap(nullptr, sizeof(FakePEB),
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  }
  memset(g_TEB, 0, sizeof(FakeTEB));
  memset(g_PEB, 0, sizeof(FakePEB));

  g_TEB->ExceptionList = 0xFFFFFFFFFFFFFFFFULL; // end of SEH chain
  g_TEB->StackBase = stack_base;
  g_TEB->StackLimit = stack_limit;
  g_TEB->Self = reinterpret_cast<uint64_t>(g_TEB);
  g_TEB->ClientId_Process = 1000;
  g_TEB->ClientId_Thread = 1001;
  g_TEB->PEB = reinterpret_cast<uint64_t>(g_PEB);

  g_PEB->BeingDebugged = 0;
  g_PEB->ImageBaseAddress = image_base;
}

// --- Shared GDT for all guest threads (all need identical 64-bit long mode CS) ---
static CPUState::gdt_segment g_GDT[32] = {};
static bool g_GDTInitialized = false;

static void SetupGuestSegments(CPUState& State) {
  if (!g_GDTInitialized) {
    auto& CS = g_GDT[CPUState::DEFAULT_USER_CS];
    CPUState::SetGDTBase(&CS, 0);
    CPUState::SetGDTLimit(&CS, 0xF'FFFFU);
    CS.L = 1;
    CS.D = 0;
    CS.P = 1;
    CS.S = 1;
    CS.Type = 0xB;
    g_GDTInitialized = true;
  }

  State.segment_arrays[CPUState::SEGMENT_ARRAY_INDEX_GDT] = g_GDT;
  State.segment_arrays[CPUState::SEGMENT_ARRAY_INDEX_LDT] = g_GDT;
  State.cs_idx = CPUState::DEFAULT_USER_CS << 3;
  State.cs_cached = CPUState::CalculateGDTBase(g_GDT[CPUState::DEFAULT_USER_CS]);
}

static void* AllocateCallRetStack(InternalThreadState* Thread) {
  const size_t CallRetSize = InternalThreadState::CALLRET_STACK_SIZE;
  const size_t Page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  const size_t AllocSize = CallRetSize + 2 * Page;

  void* AllocBase = mmap(nullptr, AllocSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (AllocBase == MAP_FAILED)
    return MAP_FAILED;

  Thread->CallRetStackBase = reinterpret_cast<uint8_t*>(AllocBase) + Page;
  if (mprotect(Thread->CallRetStackBase, CallRetSize, PROT_READ | PROT_WRITE) != 0) {
    munmap(AllocBase, AllocSize);
    Thread->CallRetStackBase = nullptr;
    return MAP_FAILED;
  }

  Thread->CurrentFrame->State.callret_sp =
    reinterpret_cast<uint64_t>(Thread->CallRetStackBase) + CallRetSize / 4;
  return AllocBase;
}

static void FreeCallRetStack(void* AllocBase) {
  if (AllocBase && AllocBase != MAP_FAILED) {
    size_t Page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    munmap(AllocBase, InternalThreadState::CALLRET_STACK_SIZE + 2 * Page);
  }
}

// --- Guest execution helper ---
// Sets up a thread, executes guest code, and returns the result.
struct GuestResult {
  uint64_t rax;
  bool exited_via_syscall;
  int exit_code;
};

static void* AllocPages(size_t size) {
  void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return (p == MAP_FAILED) ? nullptr : p;
}

static bool ExecuteGuest(FEXCore::Context::Context* CTX,
                         FEX::MacOSSyscallHandler* SyscallHandler,
                         uint64_t entry_rip, uint64_t stack_top,
                         uint64_t rdi, uint64_t rsi,
                         GuestResult& result,
                         uint64_t gs_base = 0) {
#ifdef __APPLE__
  pthread_jit_write_protect_np(false);
#endif

  auto* Thread = CTX->CreateThread(nullptr);
  auto* Frame = Thread->CurrentFrame;
  SetupGuestSegments(Frame->State);

  void* CallRetAlloc = AllocateCallRetStack(Thread);
  if (CallRetAlloc == MAP_FAILED) {
    fprintf(stderr, "Failed to allocate call-ret stack\n");
    CTX->DestroyThread(Thread);
    return false;
  }

  Frame->State.rip = entry_rip;
  Frame->State.gregs[FEXCore::X86State::REG_RSP] = stack_top;
  Frame->State.gregs[FEXCore::X86State::REG_RDI] = rdi;
  Frame->State.gregs[FEXCore::X86State::REG_RSI] = rsi;
  if (gs_base != 0)
    Frame->State.gs_cached = gs_base;

  fflush(stdout);
#ifdef __APPLE__
  pthread_jit_write_protect_np(true);
#endif
  CTX->ExecuteThread(Thread);

  result.rax = Frame->State.gregs[FEXCore::X86State::REG_RAX];
  result.exited_via_syscall = SyscallHandler->DidExit();
  result.exit_code = SyscallHandler->GetExitCode();

  CTX->DestroyThread(Thread);
  FreeCallRetStack(CallRetAlloc);
  return true;
}

// --- Inline test programs ---

static const uint8_t kTestProgram_Mov42[] = {
  0xB8, 0x2A, 0x00, 0x00, 0x00,  // mov eax, 42
  0xC3,                            // ret
};

static const uint8_t kTestProgram_Fib[] = {
  0xB9, 0x09, 0x00, 0x00, 0x00,  // mov ecx, 9
  0xB8, 0x00, 0x00, 0x00, 0x00,  // mov eax, 0
  0xBB, 0x01, 0x00, 0x00, 0x00,  // mov ebx, 1
  0x89, 0xDA,                      // mov edx, ebx
  0x01, 0xC3,                      // add ebx, eax
  0x89, 0xD0,                      // mov eax, edx
  0xE2, 0xF8,                      // loop -8
  0x89, 0xD8,                      // mov eax, ebx
  0xC3,                            // ret
};

static const uint8_t kTestProgram_Add[] = {
  0x48, 0x89, 0xF8,              // mov rax, rdi
  0x48, 0x01, 0xF0,              // add rax, rsi
  0xC3,                           // ret
};

static const uint8_t kTestProgram_CallRet[] = {
  0xE8, 0x01, 0x00, 0x00, 0x00,  // call +1
  0xC3,                            // ret (outer)
  0xB8, 0x63, 0x00, 0x00, 0x00,  // mov eax, 99
  0xC3,                            // ret (inner)
};

static const uint8_t kTestProgram_Syscall[] = {
  0xB8, 0x01, 0x00, 0x00, 0x00,  // mov eax, 1 (SYS_write)
  0xBA, 0x04, 0x00, 0x00, 0x00,  // mov edx, 4
  0x0F, 0x05,                      // syscall
  0xC3,                            // ret
};
static const char kSyscallMsg[] = "Hi!\n";

struct TestCase {
  const char* name;
  const uint8_t* code;
  size_t code_size;
  uint64_t expected_rax;
  uint64_t rdi;
  uint64_t rsi;
};

static const TestCase kTests[] = {
  {"mov eax, 42",    kTestProgram_Mov42,    sizeof(kTestProgram_Mov42),    42,  0,   0},
  {"add(100, 200)",  kTestProgram_Add,      sizeof(kTestProgram_Add),      300, 100, 200},
  {"fib(10) = 55",   kTestProgram_Fib,      sizeof(kTestProgram_Fib),      55,  0,   0},
  {"call/ret eax=99",kTestProgram_CallRet,  sizeof(kTestProgram_CallRet),  99,  0,   0},
  {"syscall write",  kTestProgram_Syscall,  sizeof(kTestProgram_Syscall),  4,   1,   (uint64_t)(uintptr_t)kSyscallMsg},
};

// --- Test runners ---

static bool RunInlineTest(FEXCore::Context::Context* CTX,
                          FEX::MacOSSyscallHandler* SyscallHandler,
                          const TestCase& Test) {
  printf("--- Test: %s ---\n", Test.name);

  const size_t kGuestCodeSize = 4096;
  const size_t kGuestStackSize = 64 * 1024;

  auto* GuestCode = static_cast<uint8_t*>(AllocPages(kGuestCodeSize));
  auto* GuestStack = static_cast<uint8_t*>(AllocPages(kGuestStackSize));
  if (!GuestCode || !GuestStack) {
    fprintf(stderr, "Failed to allocate guest memory\n");
    return false;
  }

  memcpy(GuestCode, Test.code, Test.code_size);

  uint64_t HltAddr = reinterpret_cast<uint64_t>(GuestCode) + Test.code_size;
  GuestCode[Test.code_size] = 0xF4; // HLT

  uint64_t StackTop = reinterpret_cast<uint64_t>(GuestStack) + kGuestStackSize;
  StackTop = (StackTop & ~0xFULL) - 8;
  *reinterpret_cast<uint64_t*>(StackTop) = HltAddr;

  SyscallHandler->SetGuestMemoryRegion(reinterpret_cast<uint64_t>(GuestCode), kGuestCodeSize);
  SyscallHandler->SetHltAddress(HltAddr);
  SyscallHandler->ResetExit();

  printf("  Guest code at: 0x%llx (%zu bytes)\n",
         (unsigned long long)reinterpret_cast<uint64_t>(GuestCode), Test.code_size);
  printf("  Executing...\n");

  GuestResult result{};
  if (!ExecuteGuest(CTX, SyscallHandler,
                    reinterpret_cast<uint64_t>(GuestCode), StackTop,
                    Test.rdi, Test.rsi, result)) {
    return false;
  }

  printf("  RAX result:    %llu (expected: %llu)\n", result.rax, Test.expected_rax);
  bool passed = (result.rax == Test.expected_rax);
  printf("  %s\n\n", passed ? "PASSED" : "FAILED");
  return passed;
}

static bool RunELFTest(FEXCore::Context::Context* CTX,
                       FEX::MacOSSyscallHandler* SyscallHandler,
                       const char* name,
                       const uint8_t* data, size_t size,
                       int expected_exit) {
  printf("--- ELF Test: %s ---\n", name);

  auto* buf = new uint8_t[size];
  memcpy(buf, data, size);
  TestELFs::PatchSizes(buf, size);

  ELF::LoadedELF elf{};
  if (!ELF::LoadFromBuffer(buf, size, elf)) {
    delete[] buf;
    return false;
  }
  delete[] buf;

  auto* hlt_page = static_cast<uint8_t*>(AllocPages(4096));
  auto* stack = static_cast<uint8_t*>(AllocPages(64 * 1024));
  if (!hlt_page || !stack) {
    fprintf(stderr, "Failed to allocate guest memory\n");
    return false;
  }

  hlt_page[0] = 0xF4;
  uint64_t HltAddr = reinterpret_cast<uint64_t>(hlt_page);
  uint64_t StackTop = (reinterpret_cast<uint64_t>(stack) + 64 * 1024) & ~0xFULL;

  SyscallHandler->SetGuestMemoryRegion(reinterpret_cast<uint64_t>(elf.base), elf.total_size);
  SyscallHandler->SetHltAddress(HltAddr);
  SyscallHandler->ResetExit();

  printf("  Entry: 0x%llx\n  Executing...\n", (unsigned long long)elf.entry);

  GuestResult result{};
  if (!ExecuteGuest(CTX, SyscallHandler, elf.entry, StackTop, 0, 0, result)) {
    return false;
  }

  bool passed = false;
  if (result.exited_via_syscall) {
    printf("  Guest exited with code: %d (expected: %d)\n", result.exit_code, expected_exit);
    passed = (result.exit_code == expected_exit);
  } else {
    printf("  Guest hit HLT (no exit syscall), RAX=%llu\n", result.rax);
  }
  printf("  %s\n\n", passed ? "PASSED" : "FAILED");
  return passed;
}

static bool RunPETest(FEXCore::Context::Context* CTX,
                      FEX::MacOSSyscallHandler* SyscallHandler,
                      const char* name,
                      const uint8_t* data, size_t size,
                      uint64_t expected_rax) {
  printf("--- PE Test: %s ---\n", name);

  PE::LoadedPE pe{};
  if (!PE::LoadFromBuffer(data, size, pe))
    return false;

  auto* hlt_page = static_cast<uint8_t*>(AllocPages(4096));
  auto* stack = static_cast<uint8_t*>(AllocPages(64 * 1024));
  if (!hlt_page || !stack) {
    fprintf(stderr, "Failed to allocate guest memory\n");
    return false;
  }
  hlt_page[0] = 0xF4;
  uint64_t HltAddr = reinterpret_cast<uint64_t>(hlt_page);
  uint64_t StackTop = (reinterpret_cast<uint64_t>(stack) + 64 * 1024) & ~0xFULL;

  SyscallHandler->SetGuestMemoryRegion(reinterpret_cast<uint64_t>(pe.base), pe.total_size);
  SyscallHandler->SetHltAddress(HltAddr);
  SyscallHandler->ResetExit();

  printf("  Executing from 0x%llx...\n", (unsigned long long)pe.entry);

  GuestResult result{};
  if (!ExecuteGuest(CTX, SyscallHandler, pe.entry, StackTop, 0, 0, result))
    return false;

  printf("  RAX result: %llu (expected: %llu)\n", result.rax, expected_rax);
  bool passed = (result.rax == expected_rax);
  printf("  %s\n\n", passed ? "PASSED" : "FAILED");
  return passed;
}

static bool RunPEImportTest(FEXCore::Context::Context* CTX,
                            FEX::MacOSSyscallHandler* SyscallHandler,
                            Win32::StubTable& Stubs,
                            const char* name,
                            const uint8_t* data, size_t size,
                            int expected_exit) {
  printf("--- PE Import Test: %s ---\n", name);

  PE::LoadedPE pe{};
  if (!PE::LoadFromBuffer(data, size, pe))
    return false;

  PE::ApplyRelocations(pe);

  int unresolved = PE::ResolveImports(pe, [&](const char* dll, const char* func) -> uint64_t {
    return Stubs.Resolve(dll, func);
  });
  if (unresolved > 0) {
    fprintf(stderr, "  %d unresolved imports — aborting\n", unresolved);
    return false;
  }

  auto* hlt_page = static_cast<uint8_t*>(AllocPages(4096));
  auto* stack = static_cast<uint8_t*>(AllocPages(64 * 1024));
  if (!hlt_page || !stack) {
    fprintf(stderr, "Failed to allocate guest memory\n");
    return false;
  }
  hlt_page[0] = 0xF4;
  uint64_t HltAddr = reinterpret_cast<uint64_t>(hlt_page);
  uint64_t StackTop = (reinterpret_cast<uint64_t>(stack) + 64 * 1024) & ~0xFULL;
  StackTop -= 8;
  *reinterpret_cast<uint64_t*>(StackTop) = HltAddr;

  SyscallHandler->SetGuestMemoryRegion(reinterpret_cast<uint64_t>(pe.base), pe.total_size);
  SyscallHandler->AddExecutableRegion(
    reinterpret_cast<uint64_t>(Stubs.ThunkBase()), Stubs.ThunkSize());
  SyscallHandler->SetHltAddress(HltAddr);
  SyscallHandler->ResetExit();

  printf("  Executing from 0x%llx...\n", (unsigned long long)pe.entry);

  GuestResult result{};
  if (!ExecuteGuest(CTX, SyscallHandler, pe.entry, StackTop, 0, 0, result))
    return false;

  SyscallHandler->ClearExecutableRegions();

  bool passed = false;
  if (result.exited_via_syscall) {
    printf("  Guest exited with code: %d (expected: %d)\n", result.exit_code, expected_exit);
    passed = (result.exit_code == expected_exit);
  } else {
    printf("  Guest hit HLT (no ExitProcess call), RAX=%llu\n", result.rax);
  }
  printf("  %s\n\n", passed ? "PASSED" : "FAILED");
  return passed;
}

// Detect file type by magic bytes
enum class BinaryType { Unknown, ELF, PE };
static BinaryType DetectBinaryType(const char* path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return BinaryType::Unknown;
  uint8_t magic[4] = {};
  read(fd, magic, 4);
  close(fd);
  if (magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F')
    return BinaryType::ELF;
  if (magic[0] == 'M' && magic[1] == 'Z')
    return BinaryType::PE;
  return BinaryType::Unknown;
}

// Stub resolver callback for DLL loader
static uint64_t DLLStubResolverCallback(const char* dll, const char* func, void* ctx) {
  auto* stubs = static_cast<Win32::StubTable*>(ctx);
  return stubs->Resolve(dll, func);
}

// Run a user-supplied PE from disk
static void RunUserPE(FEXCore::Context::Context* CTX,
                      FEX::MacOSSyscallHandler* SyscallHandler,
                      const char* path) {
  printf("\n=== Running PE: %s ===\n\n", path);

  PE::LoadedPE pe{};
  if (!PE::LoadFromFile(path, pe))
    return;

  PE::ApplyRelocations(pe);
  PE::DumpExceptionDirectory(pe);
  if (!PE::InitializeTLS(pe)) {
    fprintf(stderr, "TLS initialization failed\n");
    return;
  }

  Win32::StubTable stubs;
  if (!stubs.Init()) {
    fprintf(stderr, "Failed to initialize Win32 stub table\n");
    return;
  }

  // Set up DLL loader — searches for real PE DLLs before falling back to stubs
  DLL::DLLLoader dll_loader;
  dll_loader.SetStubResolver(DLLStubResolverCallback, &stubs);

  // Add search paths: directory of the .exe, then a system DLL directory if it exists
  {
    std::string exe_dir(path);
    auto slash = exe_dir.rfind('/');
    if (slash != std::string::npos)
      dll_loader.SetSearchPath(exe_dir.substr(0, slash).c_str());
    else
      dll_loader.SetSearchPath(".");
  }

  // Try to load DLLs that the exe imports — if found as PE files, they get
  // JIT-translated by FEX. If not found, imports fall back to our stub thunks.
  int unresolved = PE::ResolveImports(pe, [&](const char* dll, const char* func) -> uint64_t {
    // Try DLL loader first (loads real PE DLLs from disk)
    uint64_t addr = dll_loader.ResolveSymbol(dll, func);
    if (addr != 0)
      return addr;
    // Fall back to stubs
    return stubs.Resolve(dll, func);
  });
  if (unresolved > 0)
    printf("  Warning: %d unresolved imports (may crash)\n", unresolved);

  auto* hlt_page = static_cast<uint8_t*>(AllocPages(4096));
  size_t stack_size = 1024 * 1024;
  if (pe.stack_reserve > 0 && pe.stack_reserve <= 8 * 1024 * 1024)
    stack_size = static_cast<size_t>(pe.stack_reserve);
  auto* stack = static_cast<uint8_t*>(AllocPages(stack_size));
  if (!hlt_page || !stack) {
    fprintf(stderr, "Failed to allocate guest memory\n");
    return;
  }

  hlt_page[0] = 0xF4;
  uint64_t HltAddr = reinterpret_cast<uint64_t>(hlt_page);
  uint64_t StackTop = (reinterpret_cast<uint64_t>(stack) + stack_size) & ~0xFULL;
  auto* ret_addr = reinterpret_cast<uint64_t*>(StackTop - 8);
  *ret_addr = HltAddr;
  StackTop -= 8;

  // Set up fake TEB/PEB for Windows CRT startup
  uint64_t StackBase = reinterpret_cast<uint64_t>(stack) + stack_size;
  uint64_t StackLimit = reinterpret_cast<uint64_t>(stack);
  SetupFakeTEB(StackBase, StackLimit, reinterpret_cast<uint64_t>(pe.base));

  SyscallHandler->SetGuestMemoryRegion(reinterpret_cast<uint64_t>(pe.base), pe.total_size);
  SyscallHandler->AddExecutableRegion(
    reinterpret_cast<uint64_t>(stubs.ThunkBase()), stubs.ThunkSize());

  // Register loaded DLL memory regions as executable
  std::vector<std::pair<uint64_t, uint64_t>> dll_regions;
  dll_loader.GetAllExecutableRegions(dll_regions);
  for (auto& [base, size] : dll_regions)
    SyscallHandler->AddExecutableRegion(base, size);

  if (dll_loader.Count() > 0)
    printf("  Loaded %zu DLL(s) for JIT translation\n", dll_loader.Count());

  SyscallHandler->SetHltAddress(HltAddr);
  SyscallHandler->ResetExit();

  printf("  Entry: 0x%llx, Stack: 0x%llx\n",
         (unsigned long long)pe.entry, (unsigned long long)StackTop);
  printf("  TEB: 0x%llx, PEB: 0x%llx\n",
         (unsigned long long)reinterpret_cast<uint64_t>(g_TEB),
         (unsigned long long)reinterpret_cast<uint64_t>(g_PEB));
  printf("  Subsystem: %s\n",
         pe.subsystem == 3 ? "Console" : pe.subsystem == 2 ? "GUI" : "Unknown");
  printf("  Executing...\n");

  GuestResult result{};
  uint64_t gs_base = reinterpret_cast<uint64_t>(g_TEB);
  if (!ExecuteGuest(CTX, SyscallHandler, pe.entry, StackTop, 0, 0, result, gs_base))
    return;

  SyscallHandler->ClearExecutableRegions();

  if (result.exited_via_syscall)
    printf("\n  Guest exited with code: %d\n", result.exit_code);
  else
    printf("\n  Guest hit HLT, RAX=%llu\n", result.rax);
}

// Run a user-supplied ELF from disk
static void RunUserELF(FEXCore::Context::Context* CTX,
                       FEX::MacOSSyscallHandler* SyscallHandler,
                       const char* path) {
  printf("\n=== Running: %s ===\n\n", path);

  ELF::LoadedELF elf{};
  if (!ELF::LoadFromFile(path, elf))
    return;

  auto* hlt_page = static_cast<uint8_t*>(AllocPages(4096));
  auto* stack = static_cast<uint8_t*>(AllocPages(256 * 1024));
  if (!hlt_page || !stack) {
    fprintf(stderr, "Failed to allocate guest memory\n");
    return;
  }

  hlt_page[0] = 0xF4;
  uint64_t HltAddr = reinterpret_cast<uint64_t>(hlt_page);
  uint64_t StackTop = (reinterpret_cast<uint64_t>(stack) + 256 * 1024) & ~0xFULL;

  SyscallHandler->SetGuestMemoryRegion(reinterpret_cast<uint64_t>(elf.base), elf.total_size);
  SyscallHandler->SetHltAddress(HltAddr);
  SyscallHandler->ResetExit();

  printf("  Entry: 0x%llx, Stack: 0x%llx\n",
         (unsigned long long)elf.entry, (unsigned long long)StackTop);

  GuestResult result{};
  if (!ExecuteGuest(CTX, SyscallHandler, elf.entry, StackTop, 0, 0, result))
    return;

  if (result.exited_via_syscall)
    printf("\n  Guest exited with code: %d\n", result.exit_code);
}

// Apple's private pthread API for per-thread memory model control
extern "C" int _pthread_set_properties_self(int flag, long long val, long long unused);

static bool EnableTSO() {
  // Per-thread TSO via Apple's private pthread API (same as Rosetta 2).
  // Flag 4 = _PTHREAD_SET_SELF_MEMBW_TSO, val 1 = enable TSO ordering.
  // Only affects the calling thread — no system-wide kernel modification.
  int rc = _pthread_set_properties_self(4, 1, 0);
  if (rc == 0)
    return true;

  // Do NOT fall back to sysctlbyname("kern.tso_enable") — that modifies
  // system-wide kernel state and could destabilize other processes.
  fprintf(stderr, "Warning: _pthread_set_properties_self TSO failed (rc=%d)\n", rc);
  return false;
}

int main(int argc, char** argv) {
  printf("=== Rosetta Killer - x86_64 on Apple Silicon PoC ===\n\n");

  bool tso_enabled = EnableTSO();
  if (tso_enabled) {
    printf("Hardware TSO enabled (x86 memory ordering via Apple per-thread API)\n");
  } else {
    printf("Warning: could not enable hardware TSO (multithreaded x86 code may have ordering bugs)\n");
  }

  LogMan::Throw::InstallHandler(AssertHandler);
  LogMan::Msg::InstallHandler(LogHandler);

  FEXCore::Config::Initialize();
  FEXCore::Config::Load();
  FEXCore::Config::Set(FEXCore::Config::ConfigOption::CONFIG_IS64BIT_MODE, "1");

  printf("Detecting Apple Silicon features...\n");
  auto HostFeatures = FEX::BuildHostFeatures();
  printf("  CPU cores: %zu\n", HostFeatures.CPUMIDRs.size());
  printf("  DCache line: %u bytes\n", HostFeatures.DCacheSize());
  printf("  AES: %d, CRC: %d, Atomics: %d, SHA: %d\n",
         HostFeatures.SupportsAES, HostFeatures.SupportsCRC,
         HostFeatures.SupportsAtomics, HostFeatures.SupportsSHA);

  if (HostFeatures.DCacheSize() < 16 || HostFeatures.DCacheSize() > 256) {
    fprintf(stderr, "DCacheLineLog2 encoding looks wrong (%u bytes)\n",
            HostFeatures.DCacheSize());
    return 1;
  }

  printf("\nCreating FEXCore context...\n");
  auto CTX = FEXCore::Context::Context::CreateNewContext(HostFeatures);
  if (!CTX) {
    fprintf(stderr, "Failed to create FEXCore context!\n");
    return 1;
  }

  auto SignalDelegator = std::make_unique<FEX::MacOSSignalDelegator>();
  CTX->SetSignalDelegator(SignalDelegator.get());

  auto SyscallHandler = std::make_unique<FEX::MacOSSyscallHandler>();
  CTX->SetSyscallHandler(SyscallHandler.get());

  CTX->EnableExitOnHLT();

  // Tell FEXCore that hardware TSO is active so it skips expensive atomic
  // barriers in the JIT output — Apple Silicon enforces x86 memory ordering
  // natively when the per-thread TSO flag is set.
  if (tso_enabled)
    CTX->SetHardwareTSOSupport(true);

  pthread_jit_write_protect_np(false);

  printf("Initializing JIT core...\n");
  fflush(stdout);
  if (!CTX->InitCore()) {
    fprintf(stderr, "Failed to initialize FEXCore!\n");
    return 1;
  }
  printf("JIT core initialized!\n\n");

  // --- Inline tests ---
  int passed = 0;
  int total = sizeof(kTests) / sizeof(kTests[0]);
  for (const auto& test : kTests) {
    if (RunInlineTest(CTX.get(), SyscallHandler.get(), test))
      passed++;
  }
  printf("=== Inline tests: %d/%d passed ===\n\n", passed, total);

  // --- ELF tests ---
  printf("=== ELF Loader Tests ===\n\n");
  int elf_passed = 0, elf_total = 1;
  if (RunELFTest(CTX.get(), SyscallHandler.get(),
                 "hello_elf (write + exit)",
                 TestELFs::kHelloELF, TestELFs::kHelloELF_Size, 42))
    elf_passed++;
  printf("=== ELF tests: %d/%d passed ===\n\n", elf_passed, elf_total);

  // --- PE tests ---
  printf("=== PE Loader Tests ===\n\n");
  int pe_passed = 0, pe_total = 4;
  auto test_pe = TestPEs::BuildMinimalPE();
  if (RunPETest(CTX.get(), SyscallHandler.get(),
                "multiply 7*13=91", test_pe.data, test_pe.size, 91))
    pe_passed++;

  Win32::StubTable win32_stubs;
  if (!win32_stubs.Init()) {
    fprintf(stderr, "Failed to initialize Win32 stub table\n");
  } else {
    auto import_pe = TestPEs::BuildImportPE();
    if (RunPEImportTest(CTX.get(), SyscallHandler.get(), win32_stubs,
                        "GetStdHandle+WriteFile+ExitProcess",
                        import_pe.data, import_pe.size, 0))
      pe_passed++;
    auto msvcrt_pe = TestPEs::BuildMsvcrtPE();
    if (RunPEImportTest(CTX.get(), SyscallHandler.get(), win32_stubs,
                        "msvcrt puts + ExitProcess",
                        msvcrt_pe.data, msvcrt_pe.size, 0))
      pe_passed++;

    auto crt_pe = TestPEs::BuildCrtStartupPE();
    if (RunPEImportTest(CTX.get(), SyscallHandler.get(), win32_stubs,
                        "CRT startup sequence (MinGW-style)",
                        crt_pe.data, crt_pe.size, 0))
      pe_passed++;
  }
  printf("=== PE tests: %d/%d passed ===\n\n", pe_passed, pe_total);

  int all_passed = passed + elf_passed + pe_passed;
  int all_total = total + elf_total + pe_total;

  // Tear down FEXCore before Metal — FEX's signal delegation conflicts with Metal's threads
  SyscallHandler.reset();
  SignalDelegator.reset();
  CTX.reset();

#ifdef __APPLE__
  pthread_jit_write_protect_np(false);
#endif

  struct sigaction sa_dfl{};
  sa_dfl.sa_handler = SIG_DFL;
  sigemptyset(&sa_dfl.sa_mask);
  sigaction(SIGSEGV, &sa_dfl, nullptr);
  sigaction(SIGBUS, &sa_dfl, nullptr);
  sigaction(SIGILL, &sa_dfl, nullptr);
  sigaction(SIGTRAP, &sa_dfl, nullptr);
  // --- Metal render test ---
  printf("=== Metal Render Test ===\n\n");
  {
    Metal::MetalBackend metal;
    bool metal_ok = metal.Init();
    int metal_passed = 0, metal_total = 3;

    if (!metal_ok) {
      printf("  Metal init failed — skipping render tests\n");
    } else {
      // Test 1: Compile shaders
      printf("--- Metal Test: Shader compilation ---\n");
      const char* shader_src =
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct VertexIn { float2 position [[attribute(0)]]; float4 color [[attribute(1)]]; };\n"
        "struct VertexOut { float4 position [[position]]; float4 color; };\n"
        "vertex VertexOut vs_main(VertexIn in [[stage_in]]) {\n"
        "  VertexOut out;\n"
        "  out.position = float4(in.position, 0.0, 1.0);\n"
        "  out.color = in.color;\n"
        "  return out;\n"
        "}\n"
        "fragment float4 fs_main(VertexOut in [[stage_in]]) {\n"
        "  return in.color;\n"
        "}\n";

      auto* vs = metal.CompileShader(shader_src, "vs_main", Metal::MetalShader::Vertex);
      auto* ps = metal.CompileShader(shader_src, "fs_main", Metal::MetalShader::Pixel);
      if (vs && ps) {
        printf("  PASSED\n\n");
        metal_passed++;
      } else {
        printf("  FAILED\n\n");
      }

      // Test 2: Create pipeline + render target
      printf("--- Metal Test: Pipeline + render target ---\n");
      // Vertex layout: float2 position (offset 0) + float4 color (offset 8), stride 24
      Metal::MetalBackend::VertexAttrib attribs[] = {
        {0, Metal::MTLVertexFormatFloat2, 0, 0},   // position: float2
        {1, Metal::MTLVertexFormatFloat4, 8, 0},   // color: float4
      };
      Metal::MetalBackend::VertexLayout layouts[] = {
        {24, 0},  // stride=24 bytes (2+4 floats), buffer index 0
      };
      auto* pipeline = (vs && ps) ?
        metal.CreatePipelineState(vs, ps, Metal::MTLPixelFormatBGRA8Unorm,
                                  attribs, 2, layouts, 1) : nullptr;
      auto* rt = metal.CreateTexture2D(64, 64, Metal::MTLPixelFormatBGRA8Unorm, 0x20);
      if (pipeline && rt) {
        printf("  PASSED\n\n");
        metal_passed++;
      } else {
        printf("  FAILED (pipeline=%p rt=%p)\n\n", pipeline, rt);
      }

      // Test 3: Render a red triangle to an offscreen texture
      printf("--- Metal Test: Triangle render ---\n");
      if (pipeline && rt) {
        float verts[] = {
           0.0f,  0.5f,  1.0f, 0.0f, 0.0f, 1.0f,  // top, red
          -0.5f, -0.5f,  1.0f, 0.0f, 0.0f, 1.0f,  // bottom-left, red
           0.5f, -0.5f,  1.0f, 0.0f, 0.0f, 1.0f,  // bottom-right, red
        };
        auto* vbuf = metal.CreateBuffer(sizeof(verts), 0x1, verts);

        if (vbuf) {
          bool rp = metal.BeginRenderPass(rt->mtl_texture, 0.0f, 0.0f, 0.0f, 1.0f);
          if (rp) {
            metal.SetPipelineState(pipeline);
            metal.SetVertexBuffer(vbuf, 0, 0);
            metal.SetViewport(0, 0, 64, 64, 0, 1);
            metal.Draw(Metal::MTLPrimitiveTypeTriangle, 3, 0);
            metal.EndRenderPass();
            metal.CommitAndWait();
            printf("  GPU draw completed successfully\n");
            printf("  PASSED\n\n");
            metal_passed++;
          } else {
            printf("  BeginRenderPass failed\n  FAILED\n\n");
          }
        } else {
          printf("  Vertex buffer creation failed\n  FAILED\n\n");
        }
      } else {
        printf("  Skipped (no pipeline)\n  FAILED\n\n");
      }
    }

    printf("=== Metal tests: %d/%d passed ===\n\n", metal_passed, metal_total);
    all_total += metal_total;
    all_passed += metal_passed;
  }

  // --- User-supplied binary ---
  if (argc > 1) {
    printf("Warning: running untrusted binaries is sandboxed (fd 0-2 only, no MAP_FIXED,\n"
           "         no munmap of host memory) but not fully isolated.\n");
    auto type = DetectBinaryType(argv[1]);
    if (type == BinaryType::PE)
      RunUserPE(CTX.get(), SyscallHandler.get(), argv[1]);
    else if (type == BinaryType::ELF)
      RunUserELF(CTX.get(), SyscallHandler.get(), argv[1]);
    else
      fprintf(stderr, "Unrecognized binary format: %s\n", argv[1]);
  }

  printf("=== Total: %d/%d passed ===\n", all_passed, all_total);

  FEXCore::Config::Shutdown();
  return (all_passed == all_total) ? 0 : 1;
}
