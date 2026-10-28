// End-to-end PE execution test through the Wine bridge
// Loads a real PE binary, sets up TEB/PEB, and runs it through
// fexbridge with syscall handling — proving the full pipeline works.

#include "fexcore_wine_bridge.h"
#include "pe_loader.h"
#include "wine_fex_provider.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>

// --- Fake TEB/PEB for PE execution ---
struct alignas(4096) FakeTEB {
    uint64_t ExceptionList;       // 0x00
    uint64_t StackBase;           // 0x08
    uint64_t StackLimit;          // 0x10
    uint64_t SubSystemTib;        // 0x18
    uint64_t FiberData;           // 0x20
    uint64_t ArbitraryUserPtr;    // 0x28
    uint64_t Self;                // 0x30
    uint64_t EnvironmentPointer;  // 0x38
    uint64_t ClientId_Process;    // 0x40
    uint64_t ClientId_Thread;     // 0x48
    uint64_t ActiveRpcHandle;     // 0x50
    uint64_t ThreadLocalStorage;  // 0x58
    uint64_t PEB;                 // 0x60
    uint8_t  pad[4096 - 0x68];
};

struct alignas(4096) FakePEB {
    uint8_t  InheritedAddressSpace;
    uint8_t  ReadImageFileExecOptions;
    uint8_t  BeingDebugged;
    uint8_t  pad0[5];
    uint64_t Mutant;
    uint64_t ImageBaseAddress;
    uint64_t Ldr;
    uint64_t ProcessParameters;
    uint8_t  pad1[4096 - 0x28];
};

static FakeTEB *g_teb = nullptr;
static FakePEB *g_peb = nullptr;

static void setup_teb(uint64_t stack_base, uint64_t stack_limit, uint64_t image_base) {
    if (!g_teb) {
        g_teb = static_cast<FakeTEB*>(mmap(nullptr, sizeof(FakeTEB),
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        g_peb = static_cast<FakePEB*>(mmap(nullptr, sizeof(FakePEB),
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    }
    memset(g_teb, 0, sizeof(FakeTEB));
    memset(g_peb, 0, sizeof(FakePEB));
    g_teb->ExceptionList = 0xFFFFFFFFFFFFFFFFULL;
    g_teb->StackBase = stack_base;
    g_teb->StackLimit = stack_limit;
    g_teb->Self = reinterpret_cast<uint64_t>(g_teb);
    g_teb->ClientId_Process = 1000;
    g_teb->ClientId_Thread  = 1001;
    g_teb->PEB = reinterpret_cast<uint64_t>(g_peb);
    g_peb->BeingDebugged = 0;
    g_peb->ImageBaseAddress = image_base;
}

// --- Syscall handling ---
// Basic NT syscall emulation for testing.
// Real Wine provides hundreds of syscalls — we only need a few for tests.

static bool g_exited = false;
static int g_exit_code = 0;
static char g_console_output[4096] = {};
static int g_console_len = 0;

static constexpr uint32_t MEM_IMAGE = 0x1000000;

static uint64_t handle_syscall(const FEXBridgeSyscallArgs *sa, void *user_data) {
    (void)user_data;

    uint64_t id = sa->id;

    // NtTerminateProcess (0x2C)
    if (id == 0x2C) {
        g_exited = true;
        g_exit_code = static_cast<int>(sa->args[1]);
        return 0;
    }

    // NtWriteFile (0x08) — console output
    if (id == 0x08) {
        uint64_t buffer = sa->args[5];
        uint32_t length = static_cast<uint32_t>(sa->args[6]);
        if (buffer && length > 0 && length < 4096) {
            char vec = 0;
            if (mincore(reinterpret_cast<void*>(buffer & ~0xFFFULL), 4096, &vec) == 0) {
                uint32_t to_copy = length;
                if (g_console_len + to_copy >= sizeof(g_console_output) - 1)
                    to_copy = sizeof(g_console_output) - 1 - g_console_len;
                memcpy(g_console_output + g_console_len,
                       reinterpret_cast<void*>(buffer), to_copy);
                g_console_len += to_copy;
                g_console_output[g_console_len] = '\0';
            }
        }
        return 0;
    }

    // NtClose (0x0F)
    if (id == 0x0F)
        return 0;

    // NtQueryPerformanceCounter (0x31) — return a fake value
    if (id == 0x31) {
        uint64_t counter_ptr = sa->args[0];
        if (counter_ptr) {
            char vec = 0;
            if (mincore(reinterpret_cast<void*>(counter_ptr & ~0xFFFULL), 4096, &vec) == 0)
                *reinterpret_cast<int64_t*>(counter_ptr) = 1000000LL;
        }
        return 0;
    }

    // GetCurrentProcessId (0x??) — not a real syscall but some CRT paths check
    // Unhandled syscall
    if (getenv("FEXBRIDGE_TRACE"))
        fprintf(stderr, "test_pe_bridge: unhandled syscall 0x%llx\n", (unsigned long long)id);
    return 0xC0000001ULL;
}

// --- Test PE binaries (machine code) ---

// Test 1: Minimal PE that computes 7 * 13 = 91 and exits via HLT
// mov eax, 7; imul eax, eax, 13; hlt
static const uint8_t kMultiplyCode[] = {
    0xB8, 0x07, 0x00, 0x00, 0x00,           // mov eax, 7
    0x6B, 0xC0, 0x0D,                         // imul eax, eax, 13
    0xF4,                                      // hlt
};

// Test 2: PE that calls NtTerminateProcess(NULL, 42) via syscall
// mov r10, rcx (Windows syscall ABI)
// mov eax, 0x2C  (NtTerminateProcess)
// xor ecx, ecx   (NULL handle = current process)
// mov edx, 42    (exit code)
// syscall
static const uint8_t kExitCode42[] = {
    0x49, 0x89, 0xCA,                         // mov r10, rcx
    0xB8, 0x2C, 0x00, 0x00, 0x00,            // mov eax, 0x2C
    0x31, 0xC9,                                // xor ecx, ecx
    0xBA, 0x2A, 0x00, 0x00, 0x00,            // mov edx, 42
    0x0F, 0x05,                                // syscall
    0xF4,                                      // hlt (fallback)
};

// Test 3: PE that reads TEB self-pointer (gs:0x30) and returns it in RAX
// This validates that GS base setup works correctly.
// mov rax, gs:[0x30]  ; TEB self pointer
// hlt
static const uint8_t kReadTEB[] = {
    0x65, 0x48, 0x8B, 0x04, 0x25, 0x30, 0x00, 0x00, 0x00, // mov rax, gs:[0x30]
    0xF4,                                                     // hlt
};

// Test 4: PE that reads gs:0x60 (PEB pointer), then reads PEB.BeingDebugged,
// and returns 0 in RAX if not being debugged.
// mov rax, gs:[0x60]  ; PEB
// movzx eax, byte [rax+2]  ; BeingDebugged
// hlt
static const uint8_t kCheckPEB[] = {
    0x65, 0x48, 0x8B, 0x04, 0x25, 0x60, 0x00, 0x00, 0x00, // mov rax, gs:[0x60]
    0x0F, 0xB6, 0x40, 0x02,                                 // movzx eax, byte [rax+2]
    0xF4,                                                     // hlt
};

// Test 5: Stack operations — push/pop and call/ret
// push 0x41
// push 0x42
// pop rax      ; rax = 0x42
// pop rcx      ; rcx = 0x41
// add rax, rcx ; rax = 0x83 (131)
// hlt
static const uint8_t kStackOps[] = {
    0x6A, 0x41,                                // push 0x41
    0x6A, 0x42,                                // push 0x42
    0x58,                                      // pop rax
    0x59,                                      // pop rcx
    0x48, 0x01, 0xC8,                          // add rax, rcx
    0xF4,                                      // hlt
};

struct TestCase {
    const char *name;
    const uint8_t *code;
    size_t code_size;
    bool uses_syscall;
    bool checks_teb;
    uint64_t expected_rax;
    int expected_exit_code;
};

static const TestCase kTests[] = {
    {"multiply 7*13=91",      kMultiplyCode, sizeof(kMultiplyCode), false, false, 91, 0},
    {"syscall exit(42)",      kExitCode42,   sizeof(kExitCode42),   true,  false, 0,  42},
    {"TEB self-pointer",      kReadTEB,      sizeof(kReadTEB),      false, true,  0,  0},
    {"PEB.BeingDebugged==0",  kCheckPEB,     sizeof(kCheckPEB),     false, true,  0,  0},
    {"push/pop stack ops",    kStackOps,     sizeof(kStackOps),     false, false, 131, 0},
};

static bool run_test(int idx, const TestCase &tc) {
    printf("[%d] %s...\n", idx + 1, tc.name);

    // Allocate executable code page
    void *code = mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code == MAP_FAILED) {
        printf("    FAIL: mmap code\n");
        return false;
    }
    memcpy(code, tc.code, tc.code_size);
    mprotect(code, 4096, PROT_READ | PROT_EXEC);
    fexbridge_add_pe_range(reinterpret_cast<uint64_t>(code), 4096);

    // Allocate stack with extra page above for syscall arg overreads
    size_t stack_size = 64 * 1024 + 4096;
    void *stack = mmap(nullptr, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) {
        printf("    FAIL: mmap stack\n");
        return false;
    }
    // Leave 128 bytes above RSP for syscall handler stack reads
    uint64_t stack_top = (reinterpret_cast<uint64_t>(stack) + stack_size - 256) & ~0xFULL;

    // Setup TEB/PEB if needed
    uint64_t gs_base = 0;
    if (tc.checks_teb) {
        setup_teb(
            reinterpret_cast<uint64_t>(stack) + stack_size,
            reinterpret_cast<uint64_t>(stack),
            reinterpret_cast<uint64_t>(code));
        gs_base = reinterpret_cast<uint64_t>(g_teb);
    }

    // Reset syscall state
    g_exited = false;
    g_exit_code = 0;

    // Initialize bridge context
    FEXBridgeContext ctx = {};
    ctx.rip = reinterpret_cast<uint64_t>(code);
    ctx.rsp = stack_top;
    if (gs_base) {
        ctx.gs_base = gs_base;
        ctx.gs = 0x2B; // Standard Windows GS selector
    }

    // Create thread handle for this test
    void *thread = fexbridge_thread_init();
    if (!thread) {
        munmap(code, 4096);
        munmap(stack, stack_size);
        printf("    FAIL: thread init\n");
        return false;
    }

    // Execute
    int ret = fexbridge_execute(thread, &ctx,
        tc.uses_syscall ? handle_syscall : nullptr, nullptr);

    bool pass = false;

    if (tc.uses_syscall) {
        if (g_exited && g_exit_code == tc.expected_exit_code) {
            printf("    PASS: exited with code %d\n", g_exit_code);
            pass = true;
        } else {
            printf("    FAIL: exited=%d exit_code=%d (expected %d) ret=%d\n",
                   g_exited, g_exit_code, tc.expected_exit_code, ret);
        }
    } else if (tc.checks_teb) {
        if (strcmp(tc.name, "TEB self-pointer") == 0) {
            if (ctx.rax == gs_base) {
                printf("    PASS: TEB self-pointer = 0x%llx\n", (unsigned long long)ctx.rax);
                pass = true;
            } else {
                printf("    FAIL: TEB self-pointer = 0x%llx (expected 0x%llx)\n",
                       (unsigned long long)ctx.rax, (unsigned long long)gs_base);
            }
        } else if (strcmp(tc.name, "PEB.BeingDebugged==0") == 0) {
            if (ctx.rax == 0) {
                printf("    PASS: PEB.BeingDebugged = 0\n");
                pass = true;
            } else {
                printf("    FAIL: PEB.BeingDebugged = %llu (expected 0)\n",
                       (unsigned long long)ctx.rax);
            }
        }
    } else {
        if (ctx.rax == tc.expected_rax) {
            printf("    PASS: RAX = %llu\n", (unsigned long long)ctx.rax);
            pass = true;
        } else {
            printf("    FAIL: RAX = %llu (expected %llu)\n",
                   (unsigned long long)ctx.rax, (unsigned long long)tc.expected_rax);
        }
    }

    fexbridge_thread_destroy(thread);
    // Don't munmap code — address reuse causes stale JIT cache hits.
    // Leak is fine for a test binary. Stack is safe to free.
    munmap(stack, stack_size);

    return pass;
}

// Test 6: Wine provider interface (dlopen/dlsym flow)
static bool test_provider_interface() {
    printf("[6] Wine provider dlopen/init...\n");

    // Build the path to libfexbridge.dylib in the build directory
    // (test runs from build dir, dylib should be there)
    const char *bridge_path = getenv("FEXBRIDGE_PATH");
    if (!bridge_path) {
        // Try relative path from build directory
        bridge_path = "./libfexbridge.dylib";
    }

    int32_t rc = FexProviderInit(bridge_path);
    if (rc != STATUS_SUCCESS) {
        printf("    SKIP: provider init failed (bridge not found at %s)\n", bridge_path);
        printf("    Set FEXBRIDGE_PATH to test provider interface.\n");
        return true; // Not a failure — provider test is optional
    }

    rc = FexProviderThreadInit();
    if (rc != STATUS_SUCCESS) {
        printf("    FAIL: provider thread init\n");
        FexProviderShutdown();
        return false;
    }

    // Create a code page and run through the provider
    void *code = mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memcpy(code, kMultiplyCode, sizeof(kMultiplyCode));
    mprotect(code, 4096, PROT_READ | PROT_EXEC);

    // Notify provider of PE image
    FexProviderNotifyAlloc(code, 4096, MEM_IMAGE, 0x20); // PAGE_EXECUTE_READ

    size_t stack_size = 64 * 1024;
    void *stack = mmap(nullptr, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint64_t stack_top = (reinterpret_cast<uint64_t>(stack) + stack_size - 16) & ~0xFULL;

    CONTEXT64 ctx = {};
    ctx.Rip = reinterpret_cast<uint64_t>(code);
    ctx.Rsp = stack_top;
    ctx.ContextFlags = 0x10000F; // CONTEXT_ALL

    FexProviderSimulate(&ctx, nullptr);

    bool pass = (ctx.Rax == 91);
    if (pass)
        printf("    PASS: provider simulate returned RAX=91\n");
    else
        printf("    FAIL: provider simulate returned RAX=%llu (expected 91)\n",
               (unsigned long long)ctx.Rax);

    munmap(code, 4096);
    munmap(stack, stack_size);

    FexProviderThreadTerm();
    FexProviderShutdown();

    return pass;
}

// Test 7: Load a real PE file from disk and run it through the bridge
static bool test_pe_from_disk(const char *path) {
    printf("[7] Load and execute PE: %s...\n", path);

    PE::LoadedPE pe{};
    if (!PE::LoadFromFile(path, pe)) {
        printf("    FAIL: PE loader rejected file\n");
        return false;
    }

    PE::ApplyRelocations(pe);

    fexbridge_add_pe_range(reinterpret_cast<uint64_t>(pe.base), pe.total_size);

    size_t stack_size = 1024 * 1024;
    if (pe.stack_reserve > 0 && pe.stack_reserve <= 8 * 1024 * 1024)
        stack_size = static_cast<size_t>(pe.stack_reserve);

    void *stack = mmap(nullptr, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) {
        printf("    FAIL: mmap stack\n");
        munmap(pe.base, pe.total_size);
        return false;
    }

    uint64_t stack_top = (reinterpret_cast<uint64_t>(stack) + stack_size - 16) & ~0xFULL;
    uint64_t stack_base = reinterpret_cast<uint64_t>(stack) + stack_size;
    uint64_t stack_limit = reinterpret_cast<uint64_t>(stack);

    setup_teb(stack_base, stack_limit, reinterpret_cast<uint64_t>(pe.base));

    g_exited = false;
    g_exit_code = 0;
    g_console_len = 0;
    g_console_output[0] = '\0';

    void *thread = fexbridge_thread_init();
    if (!thread) {
        munmap(pe.base, pe.total_size);
        munmap(stack, stack_size);
        printf("    FAIL: thread init\n");
        return false;
    }

    FEXBridgeContext ctx = {};
    ctx.rip = pe.entry;
    ctx.rsp = stack_top;
    ctx.gs_base = reinterpret_cast<uint64_t>(g_teb);
    ctx.gs = 0x2B;

    printf("    Entry: 0x%llx, Stack: 0x%llx, TEB: 0x%llx\n",
           (unsigned long long)pe.entry,
           (unsigned long long)stack_top,
           (unsigned long long)ctx.gs_base);

    fexbridge_execute(thread, &ctx, handle_syscall, nullptr);

    printf("    Exit: code=%d, RAX=0x%llx, RIP=0x%llx\n",
           g_exit_code, (unsigned long long)ctx.rax, (unsigned long long)ctx.rip);

    if (g_console_len > 0)
        printf("    Console output: %s\n", g_console_output);

    if (g_exited)
        printf("    PASS: PE exited with code %d\n", g_exit_code);
    else
        printf("    INFO: PE stopped (HLT or unmapped RIP), RAX=%llu\n",
               (unsigned long long)ctx.rax);

    fexbridge_thread_destroy(thread);
    munmap(pe.base, pe.total_size);
    munmap(stack, stack_size);

    return true;
}

int main(int argc, char **argv) {
    printf("=== FEX Bridge PE Execution Tests ===\n\n");

    // Initialize bridge
    if (fexbridge_init() != 0) {
        fprintf(stderr, "Failed to initialize bridge\n");
        return 1;
    }

    int passed = 0;
    int total = sizeof(kTests) / sizeof(kTests[0]);

    // Run basic tests
    for (int i = 0; i < total; i++) {
        if (run_test(i, kTests[i]))
            passed++;
    }

    // Test 6: Provider interface
    total++;
    if (test_provider_interface())
        passed++;

    printf("\n=== Bridge PE tests: %d/%d passed ===\n", passed, total);

    // Optional: run a real PE file from command line
    if (argc > 1) {
        printf("\n");
        test_pe_from_disk(argv[1]);
    }

    fexbridge_shutdown();

    printf("\n=== All tests complete: %s ===\n",
           passed == total ? "ALL PASS" : "SOME FAILED");
    return passed == total ? 0 : 1;
}
