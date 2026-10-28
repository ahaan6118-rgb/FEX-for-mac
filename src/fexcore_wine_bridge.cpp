// FEXCore-Wine Bridge — x86_64 JIT translation for Wine on ARM64 macOS
// Provides a C interface so Wine's ntdll.so (compiled as C) can use FEXCore (C++)

#include "fexcore_wine_bridge.h"

#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/Context.h>
#include <FEXCore/Core/CoreState.h>
#include <FEXCore/Core/HostFeatures.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/Utils/LogManager.h>
#include "macos_stubs.h"

#include <atomic>
#include <csetjmp>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <pthread.h>
#include <unistd.h>

using FEXCore::Core::CPUState;
using FEXCore::Core::InternalThreadState;

// Thread-local storage for the current syscall callback
static thread_local FEXBridgeSyscallCallback tl_syscall_cb = nullptr;
static thread_local void* tl_user_data = nullptr;

static std::unique_ptr<FEXCore::Context::Context> g_CTX;
static CPUState::gdt_segment g_GDT[32] = {};
static bool g_initialized = false;

struct PERange {
    uint64_t base;
    uint64_t end;
};
static constexpr int kMaxPERanges = 256;
static PERange g_pe_ranges[kMaxPERanges] = {};
static std::atomic<int> g_pe_range_count{0};
static std::mutex g_pe_range_mutex;

// Safe native call infrastructure — catch crashes in native function calls
static thread_local sigjmp_buf tl_native_call_jmp;
static thread_local bool tl_in_native_call = false;

// Previous signal handlers — chained when fault is not during a native call
static struct sigaction g_prev_sigsegv;
static struct sigaction g_prev_sigbus;

// Forward declaration — defined below
struct ThreadHandle;

// Current thread handle accessible from fexbridge_run_callback
static thread_local ThreadHandle* tl_current_handle = nullptr;

// Deferred syscall state — win32u (table 1) syscalls are processed outside
// ExecuteThread so that KeUserModeCallback can re-enter the JIT.
static thread_local bool tl_deferred_syscall = false;
static thread_local FEXBridgeSyscallArgs tl_deferred_args;
static thread_local uint64_t tl_deferred_return_addr = 0;

// Callback state — NtCallbackReturn (syscall 0x05) is intercepted during callbacks
static thread_local int tl_callback_depth = 0;
static thread_local bool tl_callback_return_pending = false;
static thread_local uint64_t tl_callback_ret_ptr = 0;
static thread_local uint32_t tl_callback_ret_len = 0;
static thread_local int64_t tl_callback_status = 0;

// HLT page — a page with x86_64 HLT instruction, registered as PE range.
// Used to cleanly exit ExecuteThread when we need to defer a syscall.
static uint64_t g_hlt_page_addr = 0;

extern "C" int fexbridge_check_native_call_fault(void) {
    if (tl_in_native_call) {
        siglongjmp(tl_native_call_jmp, SIGSEGV);
    }
    return 0;
}

static void native_call_fault_handler(int sig, siginfo_t *info, void *ctx) {
    if (tl_in_native_call) {
        if (getenv("FEXBRIDGE_TRACE"))
            fprintf(stderr, "fexbridge: caught signal %d during native call, recovering\n", sig);
        siglongjmp(tl_native_call_jmp, sig);
    }
    // Not our fault — chain to previous handler (Wine's, or default)
    const struct sigaction& prev = (sig == SIGSEGV) ? g_prev_sigsegv : g_prev_sigbus;
    if (prev.sa_flags & SA_SIGINFO) {
        if (prev.sa_sigaction)
            prev.sa_sigaction(sig, info, ctx);
    } else {
        if (prev.sa_handler == SIG_DFL) {
            signal(sig, SIG_DFL);
            raise(sig);
        } else if (prev.sa_handler != SIG_IGN) {
            prev.sa_handler(sig);
        }
    }
}

static bool is_pe_address(uint64_t addr) {
    int count = g_pe_range_count.load(std::memory_order_acquire);
    for (int i = 0; i < count; i++) {
        if (addr >= g_pe_ranges[i].base && addr < g_pe_ranges[i].end)
            return true;
    }
    return false;
}

static void LogHandler(LogMan::DebugLevels Level, const char* Message) {
    if (!getenv("FEXBRIDGE_TRACE") && Level != LogMan::DebugLevels::ASSERT)
        return;
    fprintf(stderr, "[FEX-Wine %s] %s\n", LogMan::DebugLevelStr(Level), Message);
}

static void AssertHandler(const char* Message) {
    fprintf(stderr, "[FEX-Wine ASSERT] %s\n", Message);
}

// Syscall handler that routes guest syscalls back to Wine
class WineBridgeSyscallHandler : public FEXCore::HLE::SyscallHandler {
public:
    /* Restore FEXCore CPU state from an x86_64 CONTEXT structure.
     * Used by NtContinue/NtRaiseException to redirect JIT execution. */
    static void RestoreFromContext(CPUState& State, const uint8_t* ctx) {
        State.gregs[FEXCore::X86State::REG_RAX] = *reinterpret_cast<const uint64_t*>(ctx + 0x78);
        State.gregs[FEXCore::X86State::REG_RCX] = *reinterpret_cast<const uint64_t*>(ctx + 0x80);
        State.gregs[FEXCore::X86State::REG_RDX] = *reinterpret_cast<const uint64_t*>(ctx + 0x88);
        State.gregs[FEXCore::X86State::REG_RBX] = *reinterpret_cast<const uint64_t*>(ctx + 0x90);
        State.gregs[FEXCore::X86State::REG_RSP] = *reinterpret_cast<const uint64_t*>(ctx + 0x98);
        State.gregs[FEXCore::X86State::REG_RBP] = *reinterpret_cast<const uint64_t*>(ctx + 0xA0);
        State.gregs[FEXCore::X86State::REG_RSI] = *reinterpret_cast<const uint64_t*>(ctx + 0xA8);
        State.gregs[FEXCore::X86State::REG_RDI] = *reinterpret_cast<const uint64_t*>(ctx + 0xB0);
        State.gregs[FEXCore::X86State::REG_R8]  = *reinterpret_cast<const uint64_t*>(ctx + 0xB8);
        State.gregs[FEXCore::X86State::REG_R9]  = *reinterpret_cast<const uint64_t*>(ctx + 0xC0);
        State.gregs[FEXCore::X86State::REG_R10] = *reinterpret_cast<const uint64_t*>(ctx + 0xC8);
        State.gregs[FEXCore::X86State::REG_R11] = *reinterpret_cast<const uint64_t*>(ctx + 0xD0);
        State.gregs[FEXCore::X86State::REG_R12] = *reinterpret_cast<const uint64_t*>(ctx + 0xD8);
        State.gregs[FEXCore::X86State::REG_R13] = *reinterpret_cast<const uint64_t*>(ctx + 0xE0);
        State.gregs[FEXCore::X86State::REG_R14] = *reinterpret_cast<const uint64_t*>(ctx + 0xE8);
        State.gregs[FEXCore::X86State::REG_R15] = *reinterpret_cast<const uint64_t*>(ctx + 0xF0);
        State.rip = *reinterpret_cast<const uint64_t*>(ctx + 0xF8);
    }

    static void FillSyscallArgs(CPUState& State, uint64_t syscall_id, FEXBridgeSyscallArgs& sa) {
        memset(&sa, 0, sizeof(sa));
        sa.id = syscall_id;
        sa.args[0] = State.gregs[FEXCore::X86State::REG_R10];
        sa.args[1] = State.gregs[FEXCore::X86State::REG_RDX];
        sa.args[2] = State.gregs[FEXCore::X86State::REG_R8];
        sa.args[3] = State.gregs[FEXCore::X86State::REG_R9];
        // Read stack-passed args only if the stack region is mapped
        uint64_t rsp = State.gregs[FEXCore::X86State::REG_RSP];
        char vec = 0;
        uint64_t read_end = rsp + 13 * 8; // stack[0..12]
        if (mincore(reinterpret_cast<void*>(rsp & ~0xFFFULL), 4096, &vec) == 0 &&
            (((read_end - 1) & ~0xFFFULL) == (rsp & ~0xFFFULL) ||
             mincore(reinterpret_cast<void*>((read_end - 1) & ~0xFFFULL), 4096, &vec) == 0)) {
            auto* stack = reinterpret_cast<uint64_t*>(rsp);
            for (int i = 0; i < 8; i++)
                sa.args[4 + i] = stack[5 + i];
        }
    }

    void HandleSyscall(FEXCore::Core::CpuStateFrame* Frame) override {
        auto& State = Frame->State;
        uint64_t syscall_id = State.gregs[FEXCore::X86State::REG_RAX];
        uint64_t return_addr = State.gregs[FEXCore::X86State::REG_RCX];

        // NtCallbackReturn (syscall 0x05) during an active callback:
        // save the return values and exit the JIT so fexbridge_run_callback
        // can pick them up and return to KeUserModeCallback.
        if (syscall_id == 0x05 && tl_callback_depth > 0) {
            tl_callback_return_pending = true;
            tl_callback_ret_ptr = State.gregs[FEXCore::X86State::REG_R10];
            tl_callback_ret_len = static_cast<uint32_t>(State.gregs[FEXCore::X86State::REG_RDX]);
            tl_callback_status = static_cast<int64_t>(static_cast<int32_t>(
                State.gregs[FEXCore::X86State::REG_R8]));
            if (getenv("FEXBRIDGE_TRACE"))
                fprintf(stderr, "fexbridge: NtCallbackReturn intercepted — status=0x%llx ret_ptr=0x%llx ret_len=%u depth=%d\n",
                        (unsigned long long)tl_callback_status,
                        (unsigned long long)tl_callback_ret_ptr,
                        tl_callback_ret_len, tl_callback_depth);
            State.rip = g_hlt_page_addr;
            return;
        }

        // Win32u (table 1) syscalls are deferred: exit ExecuteThread via the HLT
        // page so KeUserModeCallback can safely re-enter the JIT.
        if (tl_syscall_cb && g_hlt_page_addr &&
            ((static_cast<uint32_t>(syscall_id) >> 12) & 3) == 1) {
            FillSyscallArgs(State, syscall_id, tl_deferred_args);
            tl_deferred_return_addr = return_addr;
            tl_deferred_syscall = true;
            if (getenv("FEXBRIDGE_TRACE"))
                fprintf(stderr, "fexbridge: deferring win32u syscall 0x%llx\n",
                        (unsigned long long)syscall_id);
            State.rip = g_hlt_page_addr;
            return;
        }

        /* NtContinue (0x43): restore x86_64 context and redirect JIT.
         * arg[0] = CONTEXT*, arg[1] = BOOLEAN raise_alert */
        if (syscall_id == 0x43) {
            uint64_t ctx_addr = State.gregs[FEXCore::X86State::REG_R10];
            if (ctx_addr) {
                auto* ctx = reinterpret_cast<const uint8_t*>(ctx_addr);
                uint32_t ctx_flags = *reinterpret_cast<const uint32_t*>(ctx);

                if (ctx_flags & 0x400000) {
                    /* ARM64 CONTEXT: Pc at offset 0x108, Sp at 0x100, X0-X28 at 0x008 */
                    uint64_t arm_pc = *reinterpret_cast<const uint64_t*>(ctx + 0x108);
                    uint64_t arm_sp = *reinterpret_cast<const uint64_t*>(ctx + 0x100);
                    if (getenv("FEXBRIDGE_TRACE"))
                        fprintf(stderr, "fexbridge: NtContinue ARM64 CONTEXT → Pc=0x%llx Sp=0x%llx X0=0x%llx\n",
                                (unsigned long long)arm_pc, (unsigned long long)arm_sp,
                                (unsigned long long)*reinterpret_cast<const uint64_t*>(ctx + 0x008));

                    if (arm_pc != 0 && arm_pc < 0x800000000000ULL) {
                        /* Map ARM64 X0-X3 → x86_64 RCX, RDX, R8, R9 (Windows calling convention) */
                        State.gregs[FEXCore::X86State::REG_RCX] = *reinterpret_cast<const uint64_t*>(ctx + 0x008);
                        State.gregs[FEXCore::X86State::REG_RDX] = *reinterpret_cast<const uint64_t*>(ctx + 0x010);
                        State.gregs[FEXCore::X86State::REG_R8]  = *reinterpret_cast<const uint64_t*>(ctx + 0x018);
                        State.gregs[FEXCore::X86State::REG_R9]  = *reinterpret_cast<const uint64_t*>(ctx + 0x020);
                        State.gregs[FEXCore::X86State::REG_RSP] = arm_sp;
                        State.rip = arm_pc;
                        return;
                    }
                } else if (ctx_flags & 0x100000) {
                    /* x86_64 CONTEXT: standard offsets */
                    uint64_t ctx_rip = *reinterpret_cast<const uint64_t*>(ctx + 0xF8);
                    if (getenv("FEXBRIDGE_TRACE"))
                        fprintf(stderr, "fexbridge: NtContinue x86_64 CONTEXT → RIP=0x%llx RSP=0x%llx\n",
                                (unsigned long long)ctx_rip,
                                (unsigned long long)*reinterpret_cast<const uint64_t*>(ctx + 0x98));
                    if (ctx_rip != 0 && ctx_rip < 0x800000000000ULL) {
                        RestoreFromContext(State, ctx);
                        return;
                    }
                }

                if (getenv("FEXBRIDGE_TRACE"))
                    fprintf(stderr, "fexbridge: NtContinue — flags=0x%x, passing to native handler\n", ctx_flags);
            }
        }

        if (tl_syscall_cb) {
            FEXBridgeSyscallArgs sa;
            FillSyscallArgs(State, syscall_id, sa);
            uint64_t result = tl_syscall_cb(&sa, tl_user_data);
            State.gregs[FEXCore::X86State::REG_RAX] = result;
        } else {
            State.gregs[FEXCore::X86State::REG_RAX] = 0xC0000001;
        }

        State.rip = return_addr;
    }

    FEXCore::HLE::ExecutableRangeInfo QueryGuestExecutableRange(FEXCore::Core::InternalThreadState*, uint64_t) override {
        return {.Base = 0, .Size = 1ULL << 47, .Writable = true};
    }

    std::optional<FEXCore::ExecutableFileSectionInfo> LookupExecutableFileSection(FEXCore::Core::InternalThreadState*, uint64_t) override {
        return std::nullopt;
    }
};

static std::unique_ptr<WineBridgeSyscallHandler> g_SyscallHandler;

static std::unique_ptr<FEX::MacOSSignalDelegator> g_SignalDelegator;

struct ThreadHandle {
    InternalThreadState* thread;
    void* callret_alloc;
};

static void SetupGDT() {
    auto& CS = g_GDT[CPUState::DEFAULT_USER_CS];
    CPUState::SetGDTBase(&CS, 0);
    CPUState::SetGDTLimit(&CS, 0xF'FFFFU);
    CS.L = 1; // Long mode (64-bit)
    CS.D = 0;
    CS.P = 1;
    CS.S = 1;
    CS.Type = 0xB;
}

static void* AllocCallRetStack(InternalThreadState* Thread) {
    const size_t Size = InternalThreadState::CALLRET_STACK_SIZE;
    const size_t Page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    const size_t AllocSize = Size + 2 * Page;

    void* Base = mmap(nullptr, AllocSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (Base == MAP_FAILED) return nullptr;

    Thread->CallRetStackBase = reinterpret_cast<uint8_t*>(Base) + Page;
    if (mprotect(Thread->CallRetStackBase, Size, PROT_READ | PROT_WRITE) != 0) {
        munmap(Base, AllocSize);
        return nullptr;
    }

    Thread->CurrentFrame->State.callret_sp =
        reinterpret_cast<uint64_t>(Thread->CallRetStackBase) + Size / 4;
    return Base;
}

extern "C" {

int fexbridge_init(void) {
    if (g_initialized) return 0;

    LogMan::Throw::InstallHandler(AssertHandler);
    LogMan::Msg::InstallHandler(LogHandler);

    FEXCore::Config::Initialize();
    FEXCore::Config::Load();
    FEXCore::Config::ReloadMetaLayer();

    // Configure for 64-bit mode
    FEXCore::Config::Set(FEXCore::Config::ConfigOption::CONFIG_IS64BIT_MODE, "1");
    // Software TSO barriers disabled — hardware TSO is enabled below via
    // _pthread_set_properties_self, which makes the JIT skip memory barriers.
    FEXCore::Config::Set(FEXCore::Config::ConfigOption::CONFIG_TSOENABLED, "0");
    FEXCore::Config::Set(FEXCore::Config::ConfigOption::CONFIG_VECTORTSOENABLED, "0");
    FEXCore::Config::Set(FEXCore::Config::ConfigOption::CONFIG_MEMCPYSETTSOENABLED, "0");

    auto HostFeatures = FEX::BuildHostFeatures();
    g_CTX = FEXCore::Context::Context::CreateNewContext(HostFeatures);
    if (!g_CTX) {
        fprintf(stderr, "fexbridge: failed to create FEXCore context\n");
        return -1;
    }

    g_SignalDelegator = std::make_unique<FEX::MacOSSignalDelegator>();
    g_SyscallHandler = std::make_unique<WineBridgeSyscallHandler>();

    g_CTX->SetSignalDelegator(g_SignalDelegator.get());
    g_CTX->SetSyscallHandler(g_SyscallHandler.get());
    g_CTX->EnableExitOnHLT();

    // Enable per-thread hardware TSO so the JIT skips memory barriers
    extern int _pthread_set_properties_self(int flag, long long val, long long unused);
    if (_pthread_set_properties_self(4, 1, 0) == 0)
        g_CTX->SetHardwareTSOSupport(true);

    g_CTX->InitCore();

    SetupGDT();

    // Install signal handler for native call crash recovery, chaining to previous
    struct sigaction sa{};
    sa.sa_sigaction = native_call_fault_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_prev_sigsegv);
    sigaction(SIGBUS, &sa, &g_prev_sigbus);

    // Allocate HLT page for clean JIT exit during deferred syscalls.
    // Registered as PE range so FEXCore compiles HLT and EnableExitOnHLT fires.
    void* hlt_page = mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANON, -1, 0);
    if (hlt_page != MAP_FAILED) {
        static_cast<uint8_t*>(hlt_page)[0] = 0xF4; // x86_64 HLT
        if (mprotect(hlt_page, 4096, PROT_READ | PROT_EXEC) != 0)
            mprotect(hlt_page, 4096, PROT_READ);
        g_hlt_page_addr = reinterpret_cast<uint64_t>(hlt_page);
        std::lock_guard<std::mutex> lock(g_pe_range_mutex);
        int idx = g_pe_range_count.load(std::memory_order_relaxed);
        if (idx < kMaxPERanges) {
            g_pe_ranges[idx].base = g_hlt_page_addr;
            g_pe_ranges[idx].end = g_hlt_page_addr + 4096;
            g_pe_range_count.store(idx + 1, std::memory_order_release);
        }
    }

    g_initialized = true;

    if (getenv("FEXBRIDGE_TRACE"))
        fprintf(stderr, "fexbridge: FEXCore initialized for x86_64 JIT translation\n");
    return 0;
}

void* fexbridge_thread_init(void) {
    if (!g_initialized) return nullptr;

#ifdef __APPLE__
    pthread_jit_write_protect_np(false);
#endif

    auto* Thread = g_CTX->CreateThread(nullptr);
    if (!Thread) return nullptr;

    auto* handle = new ThreadHandle();
    handle->thread = Thread;
    handle->callret_alloc = AllocCallRetStack(Thread);

    if (!handle->callret_alloc) {
        g_CTX->DestroyThread(Thread);
        delete handle;
        return nullptr;
    }

    // Setup segment registers for 64-bit long mode
    auto& State = Thread->CurrentFrame->State;
    State.segment_arrays[CPUState::SEGMENT_ARRAY_INDEX_GDT] = g_GDT;
    State.segment_arrays[CPUState::SEGMENT_ARRAY_INDEX_LDT] = g_GDT;
    State.cs_idx = CPUState::DEFAULT_USER_CS << 3;
    State.cs_cached = CPUState::CalculateGDTBase(g_GDT[CPUState::DEFAULT_USER_CS]);

    return handle;
}

int fexbridge_execute(void* thread_handle,
                      FEXBridgeContext* ctx,
                      FEXBridgeSyscallCallback syscall_cb,
                      void* user_data) {
    if (!thread_handle || !ctx) return -1;

    // Enable per-thread hardware TSO on this Wine thread
    extern int _pthread_set_properties_self(int flag, long long val, long long unused);
    _pthread_set_properties_self(4, 1, 0);

    auto* handle = static_cast<ThreadHandle*>(thread_handle);
    auto* Thread = handle->thread;
    auto& State = Thread->CurrentFrame->State;

    // Load x86_64 register state from context
    State.gregs[FEXCore::X86State::REG_RAX] = ctx->rax;
    State.gregs[FEXCore::X86State::REG_RCX] = ctx->rcx;
    State.gregs[FEXCore::X86State::REG_RDX] = ctx->rdx;
    State.gregs[FEXCore::X86State::REG_RBX] = ctx->rbx;
    State.gregs[FEXCore::X86State::REG_RSP] = ctx->rsp;
    State.gregs[FEXCore::X86State::REG_RBP] = ctx->rbp;
    State.gregs[FEXCore::X86State::REG_RSI] = ctx->rsi;
    State.gregs[FEXCore::X86State::REG_RDI] = ctx->rdi;
    State.gregs[FEXCore::X86State::REG_R8]  = ctx->r8;
    State.gregs[FEXCore::X86State::REG_R9]  = ctx->r9;
    State.gregs[FEXCore::X86State::REG_R10] = ctx->r10;
    State.gregs[FEXCore::X86State::REG_R11] = ctx->r11;
    State.gregs[FEXCore::X86State::REG_R12] = ctx->r12;
    State.gregs[FEXCore::X86State::REG_R13] = ctx->r13;
    State.gregs[FEXCore::X86State::REG_R14] = ctx->r14;
    State.gregs[FEXCore::X86State::REG_R15] = ctx->r15;
    State.rip = ctx->rip;

    // Setup segment bases
    if (ctx->gs_base) {
        auto GDT = State.GetSegmentFromIndex(State, (ctx->gs & 0xffff));
        State.SetGDTBase(GDT, ctx->gs_base);
        State.SetGDTLimit(GDT, 0xF'FFFFU);
        State.gs_cached = ctx->gs_base;
    }

    // Set syscall callback and thread handle for this thread
    tl_syscall_cb = syscall_cb;
    tl_user_data = user_data;
    tl_current_handle = handle;

#ifdef __APPLE__
    pthread_jit_write_protect_np(false);
#endif

    if (getenv("FEXBRIDGE_TRACE"))
        fprintf(stderr, "fexbridge: executing at RIP=0x%llx RCX=0x%llx RSP=0x%llx\n",
                (unsigned long long)State.rip,
                (unsigned long long)State.gregs[FEXCore::X86State::REG_RCX],
                (unsigned long long)State.gregs[FEXCore::X86State::REG_RSP]);

    // Execution loop: run JIT, and when it exits due to a native ARM64 call,
    // bridge the call and re-enter JIT.
    for (int native_calls = 0; native_calls < 1000; native_calls++) {
        g_CTX->CompileRIP(Thread, State.rip);
        g_CTX->ExecuteThread(Thread);

        // Deferred win32u syscall — process outside ExecuteThread so
        // callbacks can re-enter the JIT via fexbridge_run_callback.
        if (tl_deferred_syscall) {
            tl_deferred_syscall = false;
            if (getenv("FEXBRIDGE_TRACE"))
                fprintf(stderr, "fexbridge: processing deferred syscall 0x%llx\n",
                        (unsigned long long)tl_deferred_args.id);
            uint64_t result = tl_syscall_cb ? tl_syscall_cb(&tl_deferred_args, tl_user_data) : 0xC0000001ULL;
            State.gregs[FEXCore::X86State::REG_RAX] = result;
            State.rip = tl_deferred_return_addr;
#ifdef __APPLE__
            pthread_jit_write_protect_np(false);
#endif
            native_calls--; // deferred syscalls must not consume the native-call budget
            continue;
        }

        // If RIP is still in PE range (or is a HLT), execution ended normally
        if (is_pe_address(State.rip) || g_pe_range_count.load(std::memory_order_acquire) == 0)
            break;

        // RIP landed on a native ARM64 address — FEXCore couldn't decode it.
        // This means x86_64 code did `call native_addr`.
        // The return address was pushed on the x86_64 stack by the CALL instruction.
        uint64_t native_target = State.rip;
        uint64_t rsp_val = State.gregs[FEXCore::X86State::REG_RSP];

        // Validate guest stack is mapped before reading (9 uint64_t: ret + 8 args)
        {
            char vec = 0;
            if (mincore(reinterpret_cast<void*>(rsp_val & ~0xFFFULL), 4096, &vec) != 0) {
                if (getenv("FEXBRIDGE_TRACE"))
                    fprintf(stderr, "fexbridge: guest stack at 0x%llx not mapped, aborting\n",
                            (unsigned long long)rsp_val);
                break;
            }
        }

        auto* guest_stack = reinterpret_cast<uint64_t*>(rsp_val);
        uint64_t return_addr = guest_stack[0];

        if (getenv("FEXBRIDGE_TRACE")) {
            fprintf(stderr, "fexbridge: native call bridge — target=0x%llx ret=0x%llx RSP=0x%llx\n",
                    (unsigned long long)native_target,
                    (unsigned long long)return_addr,
                    (unsigned long long)State.gregs[FEXCore::X86State::REG_RSP]);
            fprintf(stderr, "fexbridge:   RCX=0x%llx RDX=0x%llx R8=0x%llx R9=0x%llx\n",
                    (unsigned long long)State.gregs[FEXCore::X86State::REG_RCX],
                    (unsigned long long)State.gregs[FEXCore::X86State::REG_RDX],
                    (unsigned long long)State.gregs[FEXCore::X86State::REG_R8],
                    (unsigned long long)State.gregs[FEXCore::X86State::REG_R9]);
            fprintf(stderr, "fexbridge:   target aligned=%d stack[0..3]=0x%llx 0x%llx 0x%llx 0x%llx\n",
                    (int)(native_target % 4 == 0),
                    (unsigned long long)guest_stack[0],
                    (unsigned long long)guest_stack[1],
                    (unsigned long long)guest_stack[2],
                    (unsigned long long)guest_stack[3]);
            uint8_t buf[8] = {};
            char vec = 0;
            if (mincore((void *)((uintptr_t)native_target & ~0xFFFUL), 4096, &vec) == 0) {
                memcpy(buf, (void *)native_target, 8);
                fprintf(stderr, "fexbridge:   target bytes: %02x %02x %02x %02x %02x %02x %02x %02x (page resident=%d)\n",
                        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], (int)(vec & 1));
            } else {
                fprintf(stderr, "fexbridge:   target address NOT MAPPED (mincore failed)\n");
            }
        }

        // Safety: skip call if target is not mapped
        {
            char vec = 0;
            if (mincore((void *)((uintptr_t)native_target & ~0xFFFUL), 4096, &vec) != 0) {
                if (getenv("FEXBRIDGE_TRACE"))
                    fprintf(stderr, "fexbridge: target 0x%llx not mapped, aborting native call\n",
                            (unsigned long long)native_target);
                break;
            }
            // ARM64 functions must be 4-byte aligned. If the target has low bits
            // set, it may be a tagged pointer (Wine ARM64EC uses low bits as flags).
            // Clear low 2 bits to get the real function address.
            if (native_target & 0x3) {
                if (getenv("FEXBRIDGE_TRACE"))
                    fprintf(stderr, "fexbridge: target 0x%llx not aligned, clearing low bits → 0x%llx\n",
                            (unsigned long long)native_target,
                            (unsigned long long)(native_target & ~0x3ULL));
                native_target &= ~0x3ULL;
            }
        }

        // Call the native ARM64 function with Windows x64 ABI args:
        // RCX=1st, RDX=2nd, R8=3rd, R9=4th
        typedef uint64_t (*native_func_t)(uint64_t, uint64_t, uint64_t, uint64_t,
                                          uint64_t, uint64_t, uint64_t, uint64_t);
        auto fn = reinterpret_cast<native_func_t>(native_target);

        uint64_t result = 0;
        tl_in_native_call = true;
        int fault = sigsetjmp(tl_native_call_jmp, 1);
        if (fault == 0) {
            result = fn(
                State.gregs[FEXCore::X86State::REG_RCX],
                State.gregs[FEXCore::X86State::REG_RDX],
                State.gregs[FEXCore::X86State::REG_R8],
                State.gregs[FEXCore::X86State::REG_R9],
                guest_stack[5], guest_stack[6], guest_stack[7], guest_stack[8]);
        } else {
            if (getenv("FEXBRIDGE_TRACE"))
                fprintf(stderr, "fexbridge: native call to 0x%llx faulted (sig=%d), returning 0\n",
                        (unsigned long long)native_target, fault);
            result = 0;
        }
        tl_in_native_call = false;

        // Pop return address and set result
        State.gregs[FEXCore::X86State::REG_RSP] += 8;
        State.gregs[FEXCore::X86State::REG_RAX] = result;
        State.rip = return_addr;

        if (getenv("FEXBRIDGE_TRACE"))
            fprintf(stderr, "fexbridge: native call returned 0x%llx, resuming at RIP=0x%llx\n",
                    (unsigned long long)result,
                    (unsigned long long)return_addr);

        if (!is_pe_address(return_addr))
            break;

#ifdef __APPLE__
        pthread_jit_write_protect_np(false);
#endif
    }

    if (getenv("FEXBRIDGE_TRACE"))
        fprintf(stderr, "fexbridge: execution returned, RIP=0x%llx RAX=0x%llx\n",
                (unsigned long long)State.rip,
                (unsigned long long)State.gregs[FEXCore::X86State::REG_RAX]);

    // Store results back to context
    ctx->rax = State.gregs[FEXCore::X86State::REG_RAX];
    ctx->rcx = State.gregs[FEXCore::X86State::REG_RCX];
    ctx->rdx = State.gregs[FEXCore::X86State::REG_RDX];
    ctx->rbx = State.gregs[FEXCore::X86State::REG_RBX];
    ctx->rsp = State.gregs[FEXCore::X86State::REG_RSP];
    ctx->rbp = State.gregs[FEXCore::X86State::REG_RBP];
    ctx->rsi = State.gregs[FEXCore::X86State::REG_RSI];
    ctx->rdi = State.gregs[FEXCore::X86State::REG_RDI];
    ctx->r8  = State.gregs[FEXCore::X86State::REG_R8];
    ctx->r9  = State.gregs[FEXCore::X86State::REG_R9];
    ctx->r10 = State.gregs[FEXCore::X86State::REG_R10];
    ctx->r11 = State.gregs[FEXCore::X86State::REG_R11];
    ctx->r12 = State.gregs[FEXCore::X86State::REG_R12];
    ctx->r13 = State.gregs[FEXCore::X86State::REG_R13];
    ctx->r14 = State.gregs[FEXCore::X86State::REG_R14];
    ctx->r15 = State.gregs[FEXCore::X86State::REG_R15];
    ctx->rip = State.rip;

    tl_syscall_cb = nullptr;
    tl_user_data = nullptr;

    return 0;
}

int64_t fexbridge_run_callback(
    uint64_t dispatcher_addr,
    uint32_t id,
    const void *args,
    uint32_t len,
    void **ret_ptr,
    uint32_t *ret_len)
{
    if (!tl_current_handle || !g_initialized) return 0xC0000001LL; // STATUS_UNSUCCESSFUL

    auto* handle = tl_current_handle;
    auto* Thread = handle->thread;
    auto& State = Thread->CurrentFrame->State;

    // Save current CPU state
    uint64_t saved_gregs[16];
    for (int i = 0; i < 16; i++)
        saved_gregs[i] = State.gregs[i];
    uint64_t saved_rip = State.rip;
    uint64_t saved_gs_cached = State.gs_cached;

    // Set up x86_64 callback stack layout on the JIT's user stack.
    // Layout matches Wine's x86_64 callback_stack_layout:
    //   0x00: padding[4] (shadow space)
    //   0x20: args pointer → args_data at 0x58
    //   0x28: len (ULONG)
    //   0x2c: id (ULONG)
    //   0x30: machine_frame (rip, cs, eflags, rsp, ss)
    //   0x58: args_data[len]
    uint64_t orig_rsp = State.gregs[FEXCore::X86State::REG_RSP];
    if (len > 16 * 1024 * 1024) {
        fprintf(stderr, "fexbridge: callback args too large (%u bytes)\n", len);
        return 0xC0000001LL;
    }
    uint64_t stack_size = ((0x58 + len + 15) & ~15ULL) + 8; // +8 for 16-byte alignment before call
    uint64_t new_rsp = (orig_rsp - stack_size) & ~15ULL;

    // Verify the callback stack region is mapped before writing
    {
        char vec = 0;
        uint64_t page_start = new_rsp & ~0xFFFULL;
        uint64_t page_end = (new_rsp + 0x58 + len + 0xFFF) & ~0xFFFULL;
        for (uint64_t p = page_start; p < page_end; p += 4096) {
            if (mincore(reinterpret_cast<void*>(p), 4096, &vec) != 0) {
                fprintf(stderr, "fexbridge: callback stack not mapped at 0x%llx\n",
                        (unsigned long long)p);
                return 0xC0000001LL;
            }
        }
    }

    auto* stack = reinterpret_cast<uint8_t*>(new_rsp);
    memset(stack, 0, 0x58);

    // args pointer → args_data
    *reinterpret_cast<uint64_t*>(stack + 0x20) = new_rsp + 0x58;
    // len and id
    *reinterpret_cast<uint32_t*>(stack + 0x28) = len;
    *reinterpret_cast<uint32_t*>(stack + 0x2c) = id;
    // machine_frame
    *reinterpret_cast<uint64_t*>(stack + 0x30) = 0;          // rip (unused — NtCallbackReturn intercepted)
    *reinterpret_cast<uint64_t*>(stack + 0x38) = 0x33;       // cs
    *reinterpret_cast<uint64_t*>(stack + 0x40) = 0x202;      // eflags (IF)
    *reinterpret_cast<uint64_t*>(stack + 0x48) = orig_rsp;   // rsp
    *reinterpret_cast<uint64_t*>(stack + 0x50) = 0x2b;       // ss
    // args_data
    if (len > 0)
        memcpy(stack + 0x58, args, len);

    // Set JIT state for callback
    State.gregs[FEXCore::X86State::REG_RSP] = new_rsp;
    State.rip = dispatcher_addr;

    if (getenv("FEXBRIDGE_TRACE"))
        fprintf(stderr, "fexbridge: callback enter — id=%u len=%u dispatcher=0x%llx RSP=0x%llx depth=%d\n",
                id, len, (unsigned long long)dispatcher_addr,
                (unsigned long long)new_rsp, tl_callback_depth + 1);

    tl_callback_depth++;
    tl_callback_return_pending = false;

#ifdef __APPLE__
    pthread_jit_write_protect_np(false);
#endif

    // Callback execution loop
    for (int iters = 0; iters < 10000; iters++) {
        g_CTX->CompileRIP(Thread, State.rip);
        g_CTX->ExecuteThread(Thread);

        // NtCallbackReturn was intercepted
        if (tl_callback_return_pending) {
            tl_callback_return_pending = false;
            break;
        }

        // Deferred win32u syscall from within callback (nested callbacks)
        if (tl_deferred_syscall) {
            tl_deferred_syscall = false;
            if (getenv("FEXBRIDGE_TRACE"))
                fprintf(stderr, "fexbridge: callback: processing deferred syscall 0x%llx\n",
                        (unsigned long long)tl_deferred_args.id);
            uint64_t result = tl_syscall_cb ? tl_syscall_cb(&tl_deferred_args, tl_user_data) : 0xC0000001ULL;
            State.gregs[FEXCore::X86State::REG_RAX] = result;
            State.rip = tl_deferred_return_addr;
#ifdef __APPLE__
            pthread_jit_write_protect_np(false);
#endif
            iters--; // deferred syscalls must not consume the iteration budget
            continue;
        }

        // Normal JIT exit (PE address = HLT or similar)
        if (is_pe_address(State.rip))
            break;

        // Native call bridge — same logic as fexbridge_execute
        uint64_t native_target = State.rip;
        uint64_t cb_rsp = State.gregs[FEXCore::X86State::REG_RSP];
        {
            char vec = 0;
            if (mincore(reinterpret_cast<void*>(cb_rsp & ~0xFFFULL), 4096, &vec) != 0)
                break;
        }
        auto* guest_stack = reinterpret_cast<uint64_t*>(cb_rsp);
        uint64_t ret_addr = guest_stack[0];

        char vec = 0;
        if (mincore(reinterpret_cast<void*>(native_target & ~0xFFFULL), 4096, &vec) != 0)
            break;
        if (native_target & 0x3)
            native_target &= ~0x3ULL;

        typedef uint64_t (*native_func_t)(uint64_t, uint64_t, uint64_t, uint64_t,
                                          uint64_t, uint64_t, uint64_t, uint64_t);
        auto fn = reinterpret_cast<native_func_t>(native_target);

        uint64_t result = 0;
        tl_in_native_call = true;
        int fault = sigsetjmp(tl_native_call_jmp, 1);
        if (fault == 0) {
            result = fn(
                State.gregs[FEXCore::X86State::REG_RCX],
                State.gregs[FEXCore::X86State::REG_RDX],
                State.gregs[FEXCore::X86State::REG_R8],
                State.gregs[FEXCore::X86State::REG_R9],
                guest_stack[5], guest_stack[6], guest_stack[7], guest_stack[8]);
        } else {
            result = 0;
        }
        tl_in_native_call = false;

        State.gregs[FEXCore::X86State::REG_RSP] += 8;
        State.gregs[FEXCore::X86State::REG_RAX] = result;
        State.rip = ret_addr;

        if (!is_pe_address(ret_addr))
            break;

#ifdef __APPLE__
        pthread_jit_write_protect_np(false);
#endif
    }

    tl_callback_depth--;

    // Extract callback return values
    int64_t status = tl_callback_status;
    if (ret_ptr)
        *ret_ptr = reinterpret_cast<void*>(tl_callback_ret_ptr);
    if (ret_len)
        *ret_len = tl_callback_ret_len;

    if (getenv("FEXBRIDGE_TRACE"))
        fprintf(stderr, "fexbridge: callback exit — status=0x%llx depth=%d\n",
                (unsigned long long)status, tl_callback_depth);

    // Restore CPU state
    for (int i = 0; i < 16; i++)
        State.gregs[i] = saved_gregs[i];
    State.rip = saved_rip;
    State.gs_cached = saved_gs_cached;

    return status;
}

void fexbridge_add_pe_range(uint64_t base, uint64_t size) {
    std::lock_guard<std::mutex> lock(g_pe_range_mutex);
    int idx = g_pe_range_count.load(std::memory_order_relaxed);
    if (idx < kMaxPERanges) {
        g_pe_ranges[idx].base = base;
        g_pe_ranges[idx].end = base + size;
        g_pe_range_count.store(idx + 1, std::memory_order_release);
        if (getenv("FEXBRIDGE_TRACE"))
            fprintf(stderr, "fexbridge: registered PE range 0x%llx - 0x%llx (%d/%d)\n",
                    (unsigned long long)base, (unsigned long long)(base + size),
                    idx + 1, kMaxPERanges);
    } else {
        fprintf(stderr, "fexbridge: PE range table full (%d), ignoring 0x%llx\n",
                kMaxPERanges, (unsigned long long)base);
    }
}

void fexbridge_invalidate(uint64_t addr, uint64_t size) {
    if (g_CTX) {
        g_CTX->InvalidateCodeBuffersCodeRange(addr, size);
    }
}

void fexbridge_thread_destroy(void* thread_handle) {
    if (!thread_handle) return;

    auto* handle = static_cast<ThreadHandle*>(thread_handle);
    if (handle->callret_alloc) {
        size_t Page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        munmap(handle->callret_alloc, InternalThreadState::CALLRET_STACK_SIZE + 2 * Page);
    }
    if (handle->thread) {
        g_CTX->DestroyThread(handle->thread);
    }
    delete handle;
}

uint64_t fexbridge_get_stop_address(void) {
    if (!g_initialized || !g_SignalDelegator) return 0;
    return g_SignalDelegator->GetConfig().ThreadStopHandlerAddressSpillSRA;
}

void fexbridge_shutdown(void) {
    if (!g_initialized) return;

    // Restore previous signal handlers before tearing down
    sigaction(SIGSEGV, &g_prev_sigsegv, nullptr);
    sigaction(SIGBUS, &g_prev_sigbus, nullptr);

    g_SyscallHandler.reset();
    g_SignalDelegator.reset();
    g_CTX.reset();

    if (g_hlt_page_addr) {
        munmap(reinterpret_cast<void*>(g_hlt_page_addr), 4096);
        g_hlt_page_addr = 0;
    }
    g_pe_range_count.store(0, std::memory_order_relaxed);
    g_initialized = false;
}

} // extern "C"
