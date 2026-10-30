/*
 * VKMT x86-64 emulator skeleton — simulation entry/exit and dispatch
 *
 * Copyright 2026 Avery Felts
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * ABI conventions (derived from ntdll/signal_arm64ec.c and llvm-mingw
 * thunk disassembly; see docs/architecture.md, M2 section):
 *
 *  - BeginSimulation: plain EC call; guest state already converted into
 *    cpu_area->ContextAmd64 by ntdll; InSimulation already 1. Never returns.
 *  - ExitToX64 / DispatchJump: entered via blr/br from exit thunks with
 *    x9 = x64 target, x0-x7 = args, sp = guest stack, lr = native return
 *    address. Guest RAX is handed back to native code in x8.
 *  - RetToEntryThunk: entered via br from entry thunks when a native call
 *    from the emulator returns; x8 = guest RAX, [sp] = x64 return address.
 */

#include <stdarg.h>

#include "vkmt.h"

WINE_DEFAULT_DEBUG_CHANNEL(vkmtx64);

extern void RetToEntryThunk(void);
extern void EcCallRet(void);

static inline CHPE_V2_CPU_AREA_INFO *vkmt_cpu_area(void)
{
    return NtCurrentTeb()->ChpeV2CpuAreaInfo;
}

static UINT cpsr_to_eflags( UINT cpsr )  /* cf. ntdll/unwind.h */
{
    UINT ret = 0x202;

    if (cpsr & 0x00200000) ret |= 0x0100;  /* trap */
    if (cpsr & 0x10000000) ret |= 0x0800;  /* overflow */
    if (cpsr & 0x20000000) ret |= 0x0001;  /* carry */
    if (cpsr & 0x40000000) ret |= 0x0040;  /* zero */
    if (cpsr & 0x80000000) ret |= 0x0080;  /* negative */
    return ret;
}

/**********************************************************************
 *           vkmt_simulate
 *
 * Common simulation loop: run the interpreter skeleton on the per-thread
 * guest context, then leave via NtContinue (native target) or terminate
 * cleanly with a loud diagnostic.  Called on the emulator stack.
 */
static void vkmt_simulate( CHPE_V2_CPU_AREA_INFO *cpu_area )
{
    ARM64EC_NT_CONTEXT *ec = cpu_area->ContextAmd64;
    struct vkmt_x64_context *ctx = vkmt_thread_context();
    struct vkmt_interp_outcome out;
    NTSTATUS status;

    if (!ctx)
    {
        ERR( "no per-thread guest context, cannot simulate\n" );
        NtTerminateProcess( GetCurrentProcess(), STATUS_NO_MEMORY );
    }

    cpu_area->InSimulation = 1;
    vkmt_context_from_ec( ctx, ec );

    TRACE( "enter simulation: thread %04lx guest rip %p rsp %p\n",
           GetCurrentThreadId(), (void *)ctx->rip, (void *)ctx->gpr[VKMT_RSP] );

    vkmt_interp_run( ctx, &out );

    vkmt_context_to_ec( ec, ctx );

    switch (out.result)
    {
    case VKMT_INTERP_EXIT_NATIVE:
    {
        ULONGLONG rsp_g = ctx->gpr[VKMT_RSP];
        ULONGLONG new_sp;
        UINT i;

        ec->Pc = out.target;
        /* x64->EC transition with no thunk in the path — ARM64EC ABI:
         * args 1-4 in x0-x3 (= rcx,rdx,r8,r9 via the aliasing), args 5-8 in
         * x4-x7 (the x64 caller put them at [rsp+0x28+8i] above the return
         * address and shadow space), args 9+ on the stack at [sp+8i] (x64
         * has them at [rsp+0x48+8i]).  Repack a generous fixed window below
         * the guest rsp, and route the return through EcCallRet + the
         * per-thread LIFO so the guest return address and post-call rsp
         * survive the ABI conversion. */
        if (ctx->ec_ret_depth < 64)
        {
            ctx->ec_ret_rip[ctx->ec_ret_depth] = *(ULONGLONG *)(ULONG_PTR)rsp_g;
            ctx->ec_ret_rsp[ctx->ec_ret_depth] = rsp_g + 8;
            ctx->ec_ret_depth++;
        }
        else ERR( "ec_ret LIFO overflow, guest return will be wrong\n" );
        ec->X4 = *(ULONGLONG *)(ULONG_PTR)(rsp_g + 0x28);
        ec->X5 = *(ULONGLONG *)(ULONG_PTR)(rsp_g + 0x30);
        ec->X6 = *(ULONGLONG *)(ULONG_PTR)(rsp_g + 0x38);
        ec->X7 = *(ULONGLONG *)(ULONG_PTR)(rsp_g + 0x40);
        new_sp = (rsp_g - 0x100) & ~15ull;
        for (i = 0; i < 30; i++)
            *(ULONGLONG *)(ULONG_PTR)(new_sp + 8 * i) =
                *(ULONGLONG *)(ULONG_PTR)(rsp_g + 0x48 + 8 * i);
        ec->Sp = new_sp;
        ec->Lr = (ULONG64)EcCallRet;
        cpu_area->InSimulation = 0;
        TRACE( "exit simulation to native pc %p (rax %p rsp %p -> ec sp %p)\n",
               (void *)out.target, (void *)ec->X8, (void *)rsp_g, (void *)new_sp );
        TRACE( "  args rcx %p rdx %p r8 %p r9 %p; stack: %p %p %p %p\n",
               (void *)ctx->gpr[VKMT_RCX], (void *)ctx->gpr[VKMT_RDX],
               (void *)ctx->gpr[VKMT_R8], (void *)ctx->gpr[VKMT_R9],
               (void *)*(ULONGLONG *)(ULONG_PTR)(new_sp),
               (void *)*(ULONGLONG *)(ULONG_PTR)(new_sp + 8),
               (void *)*(ULONGLONG *)(ULONG_PTR)(new_sp + 16),
               (void *)*(ULONGLONG *)(ULONG_PTR)(new_sp + 24) );
        if (!(status = NtContinue( (CONTEXT *)ec, FALSE ))) break;
        ERR( "NtContinue failed: %lx\n", status );
        break;
    }

    case VKMT_INTERP_EXIT_RETURN:
        /* guest executed `ret` into the RetToEntryThunk marker: hand control
         * back to the EC caller at the native return address captured on
         * entry; guest RAX is already in X8 where the thunk expects it, and
         * the thunk epilogue needs its own entry SP/FP back (the guest's
         * stack usage is discarded with the call frame). */
        if (ctx->exit_depth)
        {
            ctx->exit_depth--;
            ec->Pc = ctx->exit_lr[ctx->exit_depth];
            ec->Sp = ctx->exit_sp[ctx->exit_depth];
            ec->Fp = ctx->exit_fp[ctx->exit_depth];
        }
        else  /* top-level entry (e.g. exe entry thunk): no saved frame */
        {
            ec->Pc = ctx->native_ret ? ctx->native_ret : ec->Lr;
            ec->Sp = (ULONG64)cpu_area->EmulatorData[2];
            ec->Fp = (ULONG64)cpu_area->EmulatorData[3];
        }
        cpu_area->InSimulation = 0;
        TRACE( "guest returned to EC caller at %p (rax %p)\n",
               (void *)ec->Pc, (void *)ec->X8 );
        if (!(status = NtContinue( (CONTEXT *)ec, FALSE ))) break;
        ERR( "NtContinue failed: %lx\n", status );
        break;

    default:
        ERR( "VKMT interpreter: cannot continue guest execution at rip %p "
             "(opcode %02lx) — terminating process cleanly\n",
             (void *)ctx->rip, out.opcode );
        NtTerminateProcess( GetCurrentProcess(), STATUS_ILLEGAL_INSTRUCTION );
    }
    NtTerminateProcess( GetCurrentProcess(), STATUS_UNSUCCESSFUL );
}

/**********************************************************************
 *           vkmt_begin_simulation  (C body of BeginSimulation)
 */
void vkmt_begin_simulation(void)
{
    CHPE_V2_CPU_AREA_INFO *cpu_area = vkmt_cpu_area();
    ARM64EC_NT_CONTEXT *ec = cpu_area->ContextAmd64;

    TRACE( "BeginSimulation: thread %04lx guest rip %p rsp %p\n",
           GetCurrentThreadId(), (void *)ec->Pc, (void *)ec->Sp );
    vkmt_simulate( cpu_area );
}

/**********************************************************************
 *           vkmt_simulate_from_ec  (C body of ExitToX64/DispatchJump/RetToEntryThunk)
 *
 * plant_marker: ExitToX64/DispatchJump plant the RetToEntryThunk marker
 * on top of the guest stack, so a guest `ret` pops it and takes the
 * EXIT_RETURN path back to the native caller.
 */
void vkmt_simulate_from_ec( int plant_marker )
{
    CHPE_V2_CPU_AREA_INFO *cpu_area = vkmt_cpu_area();
    ARM64EC_NT_CONTEXT *ec = cpu_area->ContextAmd64;

    /* the asm entry captured raw NZCV into EmulatorData[1] */
    ec->AMD64_EFlags = cpsr_to_eflags( (UINT)(ULONG_PTR)cpu_area->EmulatorData[1] );
    cpu_area->EmulatorData[1] = 0;

    /* mirror context_arm_to_x64() fixups that ntdll would normally do */
    ec->ContextFlags   = CONTEXT_AMD64_ALL;
    ec->AMD64_SegCs    = 0x33;
    ec->AMD64_SegDs    = 0x2b;
    ec->AMD64_SegEs    = 0x2b;
    ec->AMD64_SegFs    = 0x53;
    ec->AMD64_SegGs    = 0x2b;
    ec->AMD64_SegSs    = 0x2b;
    if (!ec->AMD64_MxCsr) ec->AMD64_MxCsr = ec->AMD64_MxCsr_copy = 0x1f80;

    if (plant_marker == 1)
    {
        struct vkmt_x64_context *ctx = vkmt_thread_context();
        ULONGLONG thunk_sp = ec->Sp;
        ULONGLONG new_sp;
        UINT i;

        /* build the x64 call frame the guest callee expects (reverse of the
         * EXIT_NATIVE repack): [rsp]=retaddr marker, shadow space, args 5-8
         * from x4-x7 at [rsp+0x28+8i], EC stack args ([thunk_sp+0x30+8i],
         * above the exit thunk's 0x30 frame) at [rsp+0x48+8i] */
        new_sp = (thunk_sp - 0x120) & ~15ull;
        *(ULONGLONG *)(ULONG_PTR)new_sp = (ULONGLONG)RetToEntryThunk;
        *(ULONGLONG *)(ULONG_PTR)(new_sp + 0x28) = ec->X4;
        *(ULONGLONG *)(ULONG_PTR)(new_sp + 0x30) = ec->X5;
        *(ULONGLONG *)(ULONG_PTR)(new_sp + 0x38) = ec->X6;
        *(ULONGLONG *)(ULONG_PTR)(new_sp + 0x40) = ec->X7;
        for (i = 0; i < 30; i++)
            *(ULONGLONG *)(ULONG_PTR)(new_sp + 0x48 + 8 * i) =
                *(ULONGLONG *)(ULONG_PTR)(thunk_sp + 0x30 + 8 * i);
        ec->Sp = new_sp;
        /* save the thunk-entry frame: on EXIT_RETURN the guest's `ret` has
         * popped the marker, but the exit thunk epilogue must resume with
         * the exact SP/FP it had when it called us */
        cpu_area->EmulatorData[2] = (void *)(ULONG_PTR)thunk_sp;
        cpu_area->EmulatorData[3] = (void *)(ULONG_PTR)ec->Fp;
        if (ctx)
        {
            ctx->native_ret = ec->Lr;
            if (ctx->exit_depth < 64)
            {
                ctx->exit_sp[ctx->exit_depth] = thunk_sp;
                ctx->exit_fp[ctx->exit_depth] = ec->Fp;
                ctx->exit_lr[ctx->exit_depth] = ec->Lr;
                ctx->exit_depth++;
            }
            else ERR( "exit frame stack overflow\n" );
        }
    }
    else if (plant_marker == 2)  /* EcCallRet: EC callee returned from a direct x64->EC call */
    {
        struct vkmt_x64_context *ctx = vkmt_thread_context();

        if (ctx && ctx->ec_ret_depth)
        {
            ctx->ec_ret_depth--;
            ec->Pc = ctx->ec_ret_rip[ctx->ec_ret_depth];
            ec->Sp = ctx->ec_ret_rsp[ctx->ec_ret_depth];
        }
        else ERR( "EcCallRet with empty ec_ret LIFO\n" );
    }

    TRACE( "simulation from EC: thread %04lx guest rip %p rsp %p native lr %p\n",
           GetCurrentThreadId(), (void *)ec->Pc, (void *)ec->Sp, (void *)ec->Lr );
    vkmt_simulate( cpu_area );
}

/*
 * Register capture for the thunk-entered paths.  Stores the guest-visible
 * ARM64EC registers into cpu_area->ContextAmd64 (see include/winnt.h
 * ARM64EC_NT_CONTEXT for the x64<->arm64 aliasing), raw NZCV into
 * EmulatorData[1], sets InSimulation, switches to the emulator stack and
 * calls the C body.  Clobbers x9/x16/x17 only.
 *
 * The caller places the guest rip in x9 before the capture (already true
 * for ExitToX64/DispatchJump; RetToEntryThunk pops it first).
 */
#define VKMT_CAPTURE_AND_SIMULATE(first_insn, plant_marker) \
    asm( first_insn \
         "ldr x16, [x28, %[teb]]\n\t"           /* cpu area */ \
         "ldr x17, [x16, %[area_ctx]]\n\t"      /* ContextAmd64 */ \
         "str x9,  [x17, %[ctx_pc]]\n\t"        /* guest rip */ \
         "mov w9, #1\n\t" \
         "strb w9, [x16, %[area_insim]]\n\t"    /* InSimulation = 1 */ \
         "stp x0,  x1,  [x17, %[ctx_x0]]\n\t" \
         "stp x2,  x3,  [x17, %[ctx_x2]]\n\t" \
         "stp x4,  x5,  [x17, %[ctx_x4]]\n\t" \
         "stp x6,  x7,  [x17, %[ctx_x6]]\n\t" \
         "str x8,       [x17, %[ctx_x8]]\n\t" \
         "stp x10, x11, [x17, %[ctx_x10]]\n\t" \
         "str x12,      [x17, %[ctx_x12]]\n\t" \
         "str x15,      [x17, %[ctx_x15]]\n\t" \
         "stp x19, x20, [x17, %[ctx_x19]]\n\t" \
         "stp x21, x22, [x17, %[ctx_x21]]\n\t" \
         "stp x25, x26, [x17, %[ctx_x25]]\n\t" \
         "str x27,      [x17, %[ctx_x27]]\n\t" \
         "mov x9, sp\n\t" \
         "stp x9,  x29, [x17, %[ctx_sp]]\n\t"   /* Sp, Fp (adjacent) */ \
         "str x30,      [x17, %[ctx_lr]]\n\t"   /* native return address */ \
         "mrs x9, nzcv\n\t" \
         "str x9, [x16, %[area_nzcv]]\n\t"      /* raw cpsr scratch */ \
         "add x9, x17, %[ctx_v]\n\t"            /* guest xmm0-15 = v0-v15 */ \
         "stp q0,  q1,  [x9], #32\n\t" \
         "stp q2,  q3,  [x9], #32\n\t" \
         "stp q4,  q5,  [x9], #32\n\t" \
         "stp q6,  q7,  [x9], #32\n\t" \
         "stp q8,  q9,  [x9], #32\n\t" \
         "stp q10, q11, [x9], #32\n\t" \
         "stp q12, q13, [x9], #32\n\t" \
         "stp q14, q15, [x9]\n\t" \
         "ldr x9, [x16, %[area_stk]]\n\t"       /* EmulatorStackBase */ \
         "and x9, x9, #0xfffffffffffffff0\n\t" \
         "mov sp, x9\n\t" \
         "mov w0, #" #plant_marker "\n\t" \
         "bl vkmt_simulate_from_ec\n\t" \
         "brk #1\n\t" \
         :: [teb]        "i" (offsetof( TEB, ChpeV2CpuAreaInfo )), \
            [area_ctx]   "i" (offsetof( CHPE_V2_CPU_AREA_INFO, ContextAmd64 )), \
            [area_insim] "i" (offsetof( CHPE_V2_CPU_AREA_INFO, InSimulation )), \
            [area_stk]   "i" (offsetof( CHPE_V2_CPU_AREA_INFO, EmulatorStackBase )), \
            [area_nzcv]  "i" (offsetof( CHPE_V2_CPU_AREA_INFO, EmulatorData[1] )), \
            [ctx_pc]     "i" (offsetof( ARM64EC_NT_CONTEXT, Pc )), \
            [ctx_x0]     "i" (offsetof( ARM64EC_NT_CONTEXT, X0 )), \
            [ctx_x2]     "i" (offsetof( ARM64EC_NT_CONTEXT, X2 )), \
            [ctx_x4]     "i" (offsetof( ARM64EC_NT_CONTEXT, X4 )), \
            [ctx_x6]     "i" (offsetof( ARM64EC_NT_CONTEXT, X6 )), \
            [ctx_x8]     "i" (offsetof( ARM64EC_NT_CONTEXT, X8 )), \
            [ctx_x10]    "i" (offsetof( ARM64EC_NT_CONTEXT, X10 )), \
            [ctx_x12]    "i" (offsetof( ARM64EC_NT_CONTEXT, X12 )), \
            [ctx_x15]    "i" (offsetof( ARM64EC_NT_CONTEXT, X15 )), \
            [ctx_x19]    "i" (offsetof( ARM64EC_NT_CONTEXT, X19 )), \
            [ctx_x21]    "i" (offsetof( ARM64EC_NT_CONTEXT, X21 )), \
            [ctx_x25]    "i" (offsetof( ARM64EC_NT_CONTEXT, X25 )), \
            [ctx_x27]    "i" (offsetof( ARM64EC_NT_CONTEXT, X27 )), \
            [ctx_sp]     "i" (offsetof( ARM64EC_NT_CONTEXT, Sp )), \
            [ctx_lr]     "i" (offsetof( ARM64EC_NT_CONTEXT, Lr )), \
            [ctx_v]      "i" (offsetof( ARM64EC_NT_CONTEXT, V )) )

/**********************************************************************
 *           ExitToX64  (xtajit64.@)
 *
 * Implementation of __os_arm64x_dispatch_call_no_redirect.
 * Entered from exit thunks with x9 = x64 target.
 */
void __attribute__((naked)) ExitToX64(void)
{
    VKMT_CAPTURE_AND_SIMULATE( "", 1 );
}

/**********************************************************************
 *           DispatchJump  (xtajit64.@)
 *
 * Implementation of __os_arm64x_dispatch_fptr.
 * Tail jump into x64 code; same convention as ExitToX64.
 */
void __attribute__((naked)) DispatchJump(void)
{
    VKMT_CAPTURE_AND_SIMULATE( "", 1 );
}

/**********************************************************************
 *           RetToEntryThunk  (xtajit64.@)
 *
 * Implementation of __os_arm64x_dispatch_ret.
 * Entered from entry thunk epilogues with x8 = guest rax and the x64
 * return address on top of the guest stack.
 */
void __attribute__((naked)) RetToEntryThunk(void)
{
    VKMT_CAPTURE_AND_SIMULATE( "mov x8, x0\n\t"          /* EC result -> guest rax (no-op via real ientry thunks) */
                               "ldr x9, [sp], #8\n\t"    /* guest rip = x64 return address */, 0 );
}

/**********************************************************************
 *           EcCallRet  (xtajit64.@)
 *
 * Return trampoline planted in LR by EXIT_NATIVE when the guest called
 * directly into EC code (no ientry thunk in the path, so no ABI
 * conversion exists).  The guest return address and post-call rsp live
 * in the per-thread ec_ret LIFO; mode 2 of vkmt_simulate_from_ec
 * restores them.
 */
void __attribute__((naked)) EcCallRet(void)
{
    VKMT_CAPTURE_AND_SIMULATE( "mov x8, x0\n\t"          /* EC result -> guest rax */, 2 );
}

/**********************************************************************
 *           BeginSimulation  (xtajit64.@)
 *
 * Called by ntdll's emulation dispatcher with the guest state already in
 * ContextAmd64 and InSimulation = 1.  Switch to the emulator stack and
 * run the simulation loop.  Never returns.
 */
void __attribute__((naked)) BeginSimulation(void)
{
    asm( "ldr x16, [x28, %[teb]]\n\t"
         "ldr x9, [x16, %[area_stk]]\n\t"
         "and x9, x9, #0xfffffffffffffff0\n\t"
         "mov sp, x9\n\t"
         "bl vkmt_begin_simulation\n\t"
         "brk #1\n\t"
         :: [teb]      "i" (offsetof( TEB, ChpeV2CpuAreaInfo )),
            [area_stk] "i" (offsetof( CHPE_V2_CPU_AREA_INFO, EmulatorStackBase )) );
}

/**********************************************************************
 *           vkmt_reset_to_consistent_state
 *
 * Called by ntdll's exception path (prepare_exception_arm64ec) after the
 * host fault context has been converted to the x64 view, before
 * dispatching to x64/EC handlers.  When the fault happened inside the
 * interpreter (InSimulation), the converted context describes the *host*
 * state (pc inside our code); rewrite it from the live guest context so
 * the exception is presented to the guest exactly as if the faulting
 * guest instruction had executed natively, and leave simulation.
 */
void vkmt_reset_to_consistent_state( EXCEPTION_RECORD *rec, CONTEXT *context, ARM64_NT_CONTEXT *arm_ctx )
{
    ARM64EC_NT_CONTEXT *ec = (ARM64EC_NT_CONTEXT *)context;
    CHPE_V2_CPU_AREA_INFO *cpu_area = vkmt_cpu_area();
    struct vkmt_x64_context *ctx;

    TRACE( "rec %p (code %lx) context %p arm_ctx %p\n",
           rec, rec ? rec->ExceptionCode : 0, context, arm_ctx );

    if (!cpu_area || !cpu_area->InSimulation || !cpu_area->EmulatorData[0])
        return;  /* fault in native EC code: nothing to reconcile */

    ctx = cpu_area->EmulatorData[0];
    TRACE( "reconciling exception %lx to guest rip %p (host pc was %p)\n",
           rec ? rec->ExceptionCode : 0, (void *)ctx->rip, (void *)ec->Pc );

    if (rec) rec->ExceptionAddress = (void *)ctx->rip;
    vkmt_context_to_ec( ec, ctx );
    ec->ContextFlags = CONTEXT_AMD64_ALL;
    cpu_area->InSimulation = 0;
    /* the abandoned interpreter run was on the emulator stack, which the
     * exception frame has replaced; per-run state (ec_ret LIFO, branch
     * ring) simply carries on. */

    /* NOTE (M3 SEH): RtlUnwindEx's walk of EC frames dies inside
     * KiUserExceptionDispatcher's frame ("invalid frame" from
     * is_valid_arm64ec_frame) — the MSFT_OP_EC_CONTEXT restore does not
     * yield the guest's x64 frames on this stack configuration, so unwind
     * passses (__try/__finally, RtlUnwindEx past the boundary) are broken.
     * See docs/architecture.md M3e for the full analysis.  The reconcile
     * above is still required and sufficient for pass-1 dispatch (filter
     * call + except block execution). */
}
