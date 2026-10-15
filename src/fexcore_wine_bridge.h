// FEXCore-Wine Bridge — C interface for Wine's ntdll.so to call into FEXCore
// This provides x86_64 JIT translation for running Windows PE code on ARM64 macOS
#ifndef FEXCORE_WINE_BRIDGE_H
#define FEXCORE_WINE_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// x86_64 register context — matches Wine's AMD64_CONTEXT layout for the registers we care about
typedef struct {
    uint64_t rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip;
    uint32_t eflags;
    uint16_t cs, ds, es, fs, gs, ss;
    uint64_t fs_base;
    uint64_t gs_base;
} FEXBridgeContext;

// Syscall args marshalled from x86_64 Windows calling convention
typedef struct {
    uint64_t id;        // syscall number (RAX)
    uint64_t args[12];  // args[0..3] from R10,RDX,R8,R9; args[4..] from stack
} FEXBridgeSyscallArgs;

// Syscall callback — called when x86_64 code executes a syscall/int 2e
// Wine's ntdll.so provides this to route syscalls back to the Unix side
// Returns the syscall result in rax
typedef uint64_t (*FEXBridgeSyscallCallback)(
    const FEXBridgeSyscallArgs* syscall,
    void* user_data
);

// Initialize FEXCore — call once at process startup
// Returns 0 on success, -1 on failure
int fexbridge_init(void);

// Create a thread context for x86_64 execution
// Returns an opaque handle, or NULL on failure
void* fexbridge_thread_init(void);

// Start executing x86_64 code from the given context
// The syscall_cb is called whenever the guest executes a syscall
// Execution continues until the guest exits or an unhandled exception occurs
// Returns 0 on normal exit, -1 on error
int fexbridge_execute(void* thread_handle,
                      FEXBridgeContext* ctx,
                      FEXBridgeSyscallCallback syscall_cb,
                      void* user_data);

// Register PE image ranges so the bridge knows which addresses are x86_64 code
// Call for each loaded PE (ntdll.dll, kernel32.dll, user exe, etc.)
void fexbridge_add_pe_range(uint64_t base, uint64_t size);

// Invalidate JIT cache for a memory region (called when PE DLLs are loaded/unloaded)
void fexbridge_invalidate(uint64_t addr, uint64_t size);

// Destroy a thread context
void fexbridge_thread_destroy(void* thread_handle);

// Get the address that, when jumped to from a signal handler, cleanly exits
// FEXCore's ExecuteThread. Returns 0 if not available.
uint64_t fexbridge_get_stop_address(void);

// Shutdown FEXCore
void fexbridge_shutdown(void);

// Execute a user-mode callback through the JIT.
// Called from KeUserModeCallback when fexbridge is active.
// Sets up x86_64 callback stack, runs KiUserCallbackDispatcher,
// intercepts NtCallbackReturn to exit cleanly.
// Returns NTSTATUS from the callback.
int64_t fexbridge_run_callback(
    uint64_t dispatcher_addr,
    uint32_t id,
    const void *args,
    uint32_t len,
    void **ret_ptr,
    uint32_t *ret_len
);

#ifdef __cplusplus
}
#endif

#endif // FEXCORE_WINE_BRIDGE_H
