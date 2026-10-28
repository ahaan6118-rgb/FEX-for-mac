// Wine WoW64 Translation Provider Interface
// Implements the BTCpu* API that Wine's ntdll expects from xtajit64.dll
// Wraps libfexbridge to provide x86_64 → ARM64 JIT translation
#ifndef WINE_FEX_PROVIDER_H
#define WINE_FEX_PROVIDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// NT status codes
#define STATUS_SUCCESS           0x00000000L
#define STATUS_UNSUCCESSFUL      0xC0000001L
#define STATUS_NOT_IMPLEMENTED   0xC0000002L

// WoW64 CPU area — per-thread structure shared between Wine's ntdll and
// the translation provider. Wine allocates this in the thread's TEB and
// passes it to BTCpuThreadInit. The provider stores/restores the x86_64
// context here between BTCpuSimulate calls.
typedef struct _WOW64_CPURESERVED {
    uint16_t Flags;
    uint16_t MachineType;      // 0x8664 for x86_64
    // Followed by CONTEXT (at offset 4, 16-byte aligned)
} WOW64_CPURESERVED;

// x86_64 CONTEXT structure (Windows layout)
// We only define the fields we actually read/write.
typedef struct _CONTEXT64 {
    uint64_t P1Home, P2Home, P3Home, P4Home, P5Home, P6Home;
    uint32_t ContextFlags;     // 0x30
    uint32_t MxCsr;            // 0x34
    uint16_t SegCs, SegDs, SegEs, SegFs, SegGs, SegSs; // 0x38-0x42
    uint32_t EFlags;           // 0x44
    uint64_t Dr0, Dr1, Dr2, Dr3, Dr6, Dr7; // 0x48-0x78 (debug regs)
    uint64_t Rax;              // 0x78
    uint64_t Rcx;              // 0x80
    uint64_t Rdx;              // 0x88
    uint64_t Rbx;              // 0x90
    uint64_t Rsp;              // 0x98
    uint64_t Rbp;              // 0xA0
    uint64_t Rsi;              // 0xA8
    uint64_t Rdi;              // 0xB0
    uint64_t R8;               // 0xB8
    uint64_t R9;               // 0xC0
    uint64_t R10;              // 0xC8
    uint64_t R11;              // 0xD0
    uint64_t R12;              // 0xD8
    uint64_t R13;              // 0xE0
    uint64_t R14;              // 0xE8
    uint64_t R15;              // 0xF0
    uint64_t Rip;              // 0xF8
    // XMM, floating point, etc. follow but we don't use them yet
    uint8_t  _xsave_pad[512];  // Placeholder for XSAVE area
} CONTEXT64;

// Syscall callback type — Wine provides this so the provider can route
// NT syscalls back to Wine's unix layer.
typedef uint64_t (*WineSyscallDispatch)(
    uint64_t syscall_id,
    uint64_t *args,
    int arg_count
);

// Provider initialization — call once at process startup.
// bridge_path: path to libfexbridge.dylib (from FEXBRIDGE_PATH env var)
// Returns STATUS_SUCCESS or STATUS_UNSUCCESSFUL.
int32_t FexProviderInit(const char *bridge_path);

// Per-thread initialization. Called by Wine's ntdll when a new thread
// starts that will execute x86_64 code.
// Returns STATUS_SUCCESS or STATUS_UNSUCCESSFUL.
int32_t FexProviderThreadInit(void);

// Main execution entry point. Called by Wine's ntdll to execute x86_64
// code. Reads the x86_64 CONTEXT from the current thread's CPU area,
// runs JIT translation, and updates the CONTEXT when a syscall is hit.
// The syscall_dispatch callback routes syscalls to Wine.
// Returns when a syscall needs processing by Wine.
void FexProviderSimulate(CONTEXT64 *ctx, WineSyscallDispatch syscall_dispatch);

// Invalidate JIT cache for a memory region.
void FexProviderFlushCache(const void *addr, uint64_t size);

// Notify the provider that memory was allocated/freed so it can track
// PE image ranges.
void FexProviderNotifyAlloc(void *addr, uint64_t size, uint32_t type, uint32_t prot);
void FexProviderNotifyFree(void *addr, uint64_t size, uint32_t type);

// Per-thread cleanup.
void FexProviderThreadTerm(void);

// Provider shutdown — call at process exit.
void FexProviderShutdown(void);

// Execute a user-mode callback through the JIT.
// Used by KeUserModeCallback for window messages, hook procs, etc.
int64_t FexProviderRunCallback(
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

#endif // WINE_FEX_PROVIDER_H
