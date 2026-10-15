// Test the FEXCore-Wine bridge with a simple x86_64 code snippet
// This validates the bridge library works before integrating into Wine

#include "fexcore_wine_bridge.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <dlfcn.h>

// Simple x86_64 machine code that computes 40 + 2 then hits HLT to stop
// mov rax, 40
// add rax, 2
// mov [rdi], rax   (store result to pointer in RDI)
// hlt              (causes SIGSEGV which stops execution)
static const uint8_t test_code[] = {
    0x48, 0xC7, 0xC0, 0x28, 0x00, 0x00, 0x00,  // mov rax, 40
    0x48, 0x83, 0xC0, 0x02,                      // add rax, 2
    0x48, 0x89, 0x07,                              // mov [rdi], rax
    0xF4,                                          // hlt
};

// Test 2: syscall interception
// mov rax, 42       (syscall number)
// syscall            (intercepted by our handler)
// After syscall, RCX = next instruction, execution continues
// mov [rdi], rax    (store syscall result)
// hlt
static const uint8_t test_syscall_code[] = {
    0x48, 0xC7, 0xC0, 0x2A, 0x00, 0x00, 0x00,  // mov rax, 42
    0x0F, 0x05,                                    // syscall
    0x48, 0x89, 0x07,                              // mov [rdi], rax (store result)
    0xF4,                                          // hlt
};

static uint64_t test_syscall_handler(const FEXBridgeSyscallArgs* syscall, void* user_data) {
    printf("  Syscall intercepted! number = %llu\n", syscall->id);
    *static_cast<int*>(user_data) = static_cast<int>(syscall->id);
    return 0x12345678ULL;
}

int main() {
    printf("=== FEXCore-Wine Bridge Test ===\n\n");

    // Initialize the bridge
    printf("[1] Initializing FEXCore bridge...\n");
    if (fexbridge_init() != 0) {
        fprintf(stderr, "Failed to initialize bridge\n");
        return 1;
    }
    printf("    OK\n\n");

    // Create a thread
    printf("[2] Creating thread context...\n");
    void* thread = fexbridge_thread_init();
    if (!thread) {
        fprintf(stderr, "Failed to create thread\n");
        fexbridge_shutdown();
        return 1;
    }
    printf("    OK\n\n");

    // Map the test code into executable memory
    printf("[3] Loading x86_64 test code (%zu bytes)...\n", sizeof(test_code));
    void* code_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code_mem == MAP_FAILED) {
        fprintf(stderr, "Failed to mmap code\n");
        fexbridge_thread_destroy(thread);
        fexbridge_shutdown();
        return 1;
    }
    memcpy(code_mem, test_code, sizeof(test_code));
    mprotect(code_mem, 4096, PROT_READ | PROT_EXEC);
    printf("    Code at %p\n\n", code_mem);

    // Allocate result storage
    uint64_t result_val = 0;
    void* result_mem = &result_val;

    // Set up a stack
    void* stack_mem = mmap(NULL, 64 * 1024, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint64_t stack_top = (uint64_t)stack_mem + 64 * 1024 - 16;

    // Setup context: RDI = pointer to result storage
    printf("[4] Setting up x86_64 context and executing...\n");
    printf("    RDI (result ptr) = %p\n", result_mem);
    FEXBridgeContext ctx = {};
    ctx.rip = (uint64_t)code_mem;
    ctx.rsp = stack_top;
    ctx.rdi = (uint64_t)result_mem;

    // Execute — will stop on HLT (which FEXCore turns into SIGSEGV)
    int ret = fexbridge_execute(thread, &ctx, nullptr, nullptr);
    printf("    Execute returned: %d\n", ret);
    printf("    RAX after execution: %llu\n", ctx.rax);
    printf("    Result at [RDI]: %llu\n\n", result_val);

    // Verify
    if (result_val == 42) {
        printf("[PASS] x86_64 code computed 40 + 2 = 42 and stored to memory!\n");
        printf("       FEXCore JIT bridge is working correctly.\n");
    } else if (ctx.rax == 42) {
        printf("[PASS] x86_64 code computed 40 + 2 = 42 (in RAX)!\n");
        printf("       FEXCore JIT bridge is working correctly.\n");
    } else {
        printf("[FAIL] Expected 42, got result_val=%llu, rax=%llu\n", result_val, ctx.rax);
    }

    // Don't munmap code_mem — keep it so next mmap gets a different address

    // ===== Test 2: Syscall interception =====
    printf("\n[5] Test 2: Syscall interception...\n");
    void* code_mem2 = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memcpy(code_mem2, test_syscall_code, sizeof(test_syscall_code));
    mprotect(code_mem2, 4096, PROT_READ | PROT_EXEC);
    printf("    Code2 at %p\n", code_mem2);

    uint64_t syscall_result = 0;
    int syscall_num_seen = -1;
    FEXBridgeContext ctx2 = {};
    ctx2.rip = (uint64_t)code_mem2;
    ctx2.rsp = stack_top;
    ctx2.rdi = (uint64_t)&syscall_result;

    int ret2 = fexbridge_execute(thread, &ctx2, test_syscall_handler, &syscall_num_seen);
    printf("    Execute returned: %d\n", ret2);
    printf("    Syscall number seen: %d\n", syscall_num_seen);
    printf("    RAX after execution: 0x%llx\n", ctx2.rax);
    printf("    Result stored to [RDI]: 0x%llx\n\n", syscall_result);

    bool test2_pass = (syscall_num_seen == 42 && syscall_result == 0x12345678ULL);
    if (test2_pass) {
        printf("[PASS] Syscall intercepted (num=42), result 0x12345678 stored!\n");
    } else {
        printf("[FAIL] Syscall test failed\n");
    }

    // Cleanup
    munmap(code_mem, 4096);
    munmap(code_mem2, 4096);
    munmap(stack_mem, 64 * 1024);
    fexbridge_thread_destroy(thread);
    fexbridge_shutdown();

    bool all_pass = (result_val == 42 || ctx.rax == 42) && test2_pass;
    printf("\n=== Bridge test complete: %s ===\n", all_pass ? "ALL PASS" : "SOME FAILED");
    return all_pass ? 0 : 1;
}
