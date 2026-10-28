// Wine WoW64 Translation Provider — wraps libfexbridge.dylib for Wine integration
// Implements the BTCpu-like interface that Wine's ntdll calls for x86_64 JIT.
//
// Architecture:
//   Wine ntdll (ARM64 native) → this provider → libfexbridge → FEXCore JIT
//
// The provider manages per-thread bridge handles and translates between Wine's
// CONTEXT structure and the bridge's FEXBridgeContext.

#include "wine_fex_provider.h"
#include "fexcore_wine_bridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <pthread.h>
#include <atomic>

// Bridge function pointers — resolved at runtime via dlopen/dlsym
static void *g_bridge_handle = nullptr;

typedef int (*pfn_init)(void);
typedef void* (*pfn_thread_init)(void);
typedef int (*pfn_execute)(void*, FEXBridgeContext*, FEXBridgeSyscallCallback, void*);
typedef void (*pfn_add_pe_range)(uint64_t, uint64_t);
typedef void (*pfn_invalidate)(uint64_t, uint64_t);
typedef void (*pfn_thread_destroy)(void*);
typedef void (*pfn_shutdown)(void);
typedef int64_t (*pfn_run_callback)(uint64_t, uint32_t, const void*, uint32_t, void**, uint32_t*);

static pfn_init           fn_init;
static pfn_thread_init    fn_thread_init;
static pfn_execute        fn_execute;
static pfn_add_pe_range   fn_add_pe_range;
static pfn_invalidate     fn_invalidate;
static pfn_thread_destroy fn_thread_destroy;
static pfn_shutdown       fn_shutdown;
static pfn_run_callback   fn_run_callback;

static std::atomic<bool> g_provider_ready{false};

// Per-thread state
static thread_local void *tl_thread_handle = nullptr;
static thread_local bool tl_thread_initialized = false;

// Memory allocation type flags (from Windows NT)
static constexpr uint32_t MEM_IMAGE = 0x1000000;

static bool resolve_bridge_symbols(void *handle) {
    fn_init           = (pfn_init)dlsym(handle, "fexbridge_init");
    fn_thread_init    = (pfn_thread_init)dlsym(handle, "fexbridge_thread_init");
    fn_execute        = (pfn_execute)dlsym(handle, "fexbridge_execute");
    fn_add_pe_range   = (pfn_add_pe_range)dlsym(handle, "fexbridge_add_pe_range");
    fn_invalidate     = (pfn_invalidate)dlsym(handle, "fexbridge_invalidate");
    fn_thread_destroy = (pfn_thread_destroy)dlsym(handle, "fexbridge_thread_destroy");
    fn_shutdown       = (pfn_shutdown)dlsym(handle, "fexbridge_shutdown");
    fn_run_callback   = (pfn_run_callback)dlsym(handle, "fexbridge_run_callback");

    if (!fn_init || !fn_thread_init || !fn_execute || !fn_add_pe_range ||
        !fn_invalidate || !fn_thread_destroy || !fn_shutdown || !fn_run_callback) {
        fprintf(stderr, "wine_fex_provider: missing bridge symbols\n");
        if (!fn_init)           fprintf(stderr, "  missing: fexbridge_init\n");
        if (!fn_thread_init)    fprintf(stderr, "  missing: fexbridge_thread_init\n");
        if (!fn_execute)        fprintf(stderr, "  missing: fexbridge_execute\n");
        if (!fn_add_pe_range)   fprintf(stderr, "  missing: fexbridge_add_pe_range\n");
        if (!fn_invalidate)     fprintf(stderr, "  missing: fexbridge_invalidate\n");
        if (!fn_thread_destroy) fprintf(stderr, "  missing: fexbridge_thread_destroy\n");
        if (!fn_shutdown)       fprintf(stderr, "  missing: fexbridge_shutdown\n");
        if (!fn_run_callback)   fprintf(stderr, "  missing: fexbridge_run_callback\n");
        return false;
    }
    return true;
}

// Translate Wine CONTEXT64 → FEXBridgeContext
static void context_to_bridge(const CONTEXT64 *ctx, FEXBridgeContext *bc) {
    bc->rax = ctx->Rax;
    bc->rcx = ctx->Rcx;
    bc->rdx = ctx->Rdx;
    bc->rbx = ctx->Rbx;
    bc->rsp = ctx->Rsp;
    bc->rbp = ctx->Rbp;
    bc->rsi = ctx->Rsi;
    bc->rdi = ctx->Rdi;
    bc->r8  = ctx->R8;
    bc->r9  = ctx->R9;
    bc->r10 = ctx->R10;
    bc->r11 = ctx->R11;
    bc->r12 = ctx->R12;
    bc->r13 = ctx->R13;
    bc->r14 = ctx->R14;
    bc->r15 = ctx->R15;
    bc->rip = ctx->Rip;
    bc->eflags = ctx->EFlags;
    bc->cs  = ctx->SegCs;
    bc->ds  = ctx->SegDs;
    bc->es  = ctx->SegEs;
    bc->fs  = ctx->SegFs;
    bc->gs  = ctx->SegGs;
    bc->ss  = ctx->SegSs;
}

// Translate FEXBridgeContext → Wine CONTEXT64
static void bridge_to_context(const FEXBridgeContext *bc, CONTEXT64 *ctx) {
    ctx->Rax = bc->rax;
    ctx->Rcx = bc->rcx;
    ctx->Rdx = bc->rdx;
    ctx->Rbx = bc->rbx;
    ctx->Rsp = bc->rsp;
    ctx->Rbp = bc->rbp;
    ctx->Rsi = bc->rsi;
    ctx->Rdi = bc->rdi;
    ctx->R8  = bc->r8;
    ctx->R9  = bc->r9;
    ctx->R10 = bc->r10;
    ctx->R11 = bc->r11;
    ctx->R12 = bc->r12;
    ctx->R13 = bc->r13;
    ctx->R14 = bc->r14;
    ctx->R15 = bc->r15;
    ctx->Rip = bc->rip;
    ctx->EFlags = bc->eflags;
}

// Syscall callback state — set per-simulate call
static thread_local WineSyscallDispatch tl_syscall_dispatch = nullptr;

static uint64_t provider_syscall_handler(const FEXBridgeSyscallArgs *sa, void *user_data) {
    (void)user_data;
    if (!tl_syscall_dispatch)
        return 0xC0000001ULL; // STATUS_UNSUCCESSFUL

    // Marshal bridge syscall args into a flat array for Wine
    uint64_t args[12];
    for (int i = 0; i < 12; i++)
        args[i] = sa->args[i];

    return tl_syscall_dispatch(sa->id, args, 12);
}

extern "C" {

int32_t FexProviderInit(const char *bridge_path) {
    if (g_provider_ready.load(std::memory_order_acquire))
        return STATUS_SUCCESS;

    const char *path = bridge_path;
    if (!path || path[0] == '\0')
        path = getenv("FEXBRIDGE_PATH");
    if (!path || path[0] == '\0') {
        fprintf(stderr, "wine_fex_provider: no bridge path (set FEXBRIDGE_PATH)\n");
        return STATUS_UNSUCCESSFUL;
    }

    g_bridge_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!g_bridge_handle) {
        fprintf(stderr, "wine_fex_provider: dlopen(%s): %s\n", path, dlerror());
        return STATUS_UNSUCCESSFUL;
    }

    if (!resolve_bridge_symbols(g_bridge_handle)) {
        dlclose(g_bridge_handle);
        g_bridge_handle = nullptr;
        return STATUS_UNSUCCESSFUL;
    }

    int rc = fn_init();
    if (rc != 0) {
        fprintf(stderr, "wine_fex_provider: fexbridge_init failed (%d)\n", rc);
        dlclose(g_bridge_handle);
        g_bridge_handle = nullptr;
        return STATUS_UNSUCCESSFUL;
    }

    g_provider_ready.store(true, std::memory_order_release);

    if (getenv("FEXBRIDGE_TRACE"))
        fprintf(stderr, "wine_fex_provider: initialized (bridge=%s)\n", path);

    return STATUS_SUCCESS;
}

int32_t FexProviderThreadInit(void) {
    if (!g_provider_ready.load(std::memory_order_acquire))
        return STATUS_UNSUCCESSFUL;

    if (tl_thread_initialized)
        return STATUS_SUCCESS;

    tl_thread_handle = fn_thread_init();
    if (!tl_thread_handle) {
        fprintf(stderr, "wine_fex_provider: fexbridge_thread_init failed\n");
        return STATUS_UNSUCCESSFUL;
    }

    tl_thread_initialized = true;

    if (getenv("FEXBRIDGE_TRACE"))
        fprintf(stderr, "wine_fex_provider: thread %p initialized\n", (void*)pthread_self());

    return STATUS_SUCCESS;
}

void FexProviderSimulate(CONTEXT64 *ctx, WineSyscallDispatch syscall_dispatch) {
    if (!g_provider_ready.load(std::memory_order_acquire) || !tl_thread_handle)
        return;

    FEXBridgeContext bc = {};
    context_to_bridge(ctx, &bc);

    tl_syscall_dispatch = syscall_dispatch;

    fn_execute(tl_thread_handle, &bc, provider_syscall_handler, nullptr);

    tl_syscall_dispatch = nullptr;

    bridge_to_context(&bc, ctx);
}

void FexProviderFlushCache(const void *addr, uint64_t size) {
    if (!g_provider_ready.load(std::memory_order_acquire))
        return;
    fn_invalidate(reinterpret_cast<uint64_t>(addr), size);
}

void FexProviderNotifyAlloc(void *addr, uint64_t size, uint32_t type, uint32_t prot) {
    if (!g_provider_ready.load(std::memory_order_acquire))
        return;

    // Register PE image mappings so the bridge knows which addresses are x86_64
    if (type & MEM_IMAGE) {
        fn_add_pe_range(reinterpret_cast<uint64_t>(addr), size);
        if (getenv("FEXBRIDGE_TRACE"))
            fprintf(stderr, "wine_fex_provider: PE image at %p size 0x%llx\n",
                    addr, (unsigned long long)size);
    }
}

void FexProviderNotifyFree(void *addr, uint64_t size, uint32_t type) {
    if (!g_provider_ready.load(std::memory_order_acquire))
        return;

    // JIT cache invalidation for freed memory
    if (type & MEM_IMAGE)
        fn_invalidate(reinterpret_cast<uint64_t>(addr), size);
}

void FexProviderThreadTerm(void) {
    if (!tl_thread_initialized)
        return;

    fn_thread_destroy(tl_thread_handle);
    tl_thread_handle = nullptr;
    tl_thread_initialized = false;

    if (getenv("FEXBRIDGE_TRACE"))
        fprintf(stderr, "wine_fex_provider: thread %p terminated\n", (void*)pthread_self());
}

void FexProviderShutdown(void) {
    if (!g_provider_ready.load(std::memory_order_acquire))
        return;

    // Clean up thread-local state if still alive
    if (tl_thread_initialized)
        FexProviderThreadTerm();

    fn_shutdown();

    if (g_bridge_handle) {
        dlclose(g_bridge_handle);
        g_bridge_handle = nullptr;
    }

    g_provider_ready.store(false, std::memory_order_release);

    if (getenv("FEXBRIDGE_TRACE"))
        fprintf(stderr, "wine_fex_provider: shutdown complete\n");
}

int64_t FexProviderRunCallback(
    uint64_t dispatcher_addr,
    uint32_t id,
    const void *args,
    uint32_t len,
    void **ret_ptr,
    uint32_t *ret_len)
{
    if (!g_provider_ready.load(std::memory_order_acquire))
        return STATUS_UNSUCCESSFUL;

    return fn_run_callback(dispatcher_addr, id, args, len, ret_ptr, ret_len);
}

} // extern "C"
