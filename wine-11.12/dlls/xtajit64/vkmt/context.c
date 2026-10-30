/*
 * VKMT x86-64 emulator skeleton — process/thread lifecycle and guest context
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
 */

#include <stdarg.h>

#include "vkmt.h"

WINE_DEFAULT_DEBUG_CHANNEL(vkmtx64);

static inline CHPE_V2_CPU_AREA_INFO *vkmt_cpu_area(void)
{
    return NtCurrentTeb()->ChpeV2CpuAreaInfo;
}

/**********************************************************************
 *           vkmt_context_from_ec
 *
 * Snapshot the live EC context into the emulator-private guest context.
 */
void vkmt_context_from_ec( struct vkmt_x64_context *ctx, const ARM64EC_NT_CONTEXT *ec )
{
    ctx->gpr[VKMT_RAX] = ec->X8;  ctx->gpr[VKMT_RCX] = ec->X0;
    ctx->gpr[VKMT_RDX] = ec->X1;  ctx->gpr[VKMT_RBX] = ec->X27;
    ctx->gpr[VKMT_RSP] = ec->Sp;  ctx->gpr[VKMT_RBP] = ec->Fp;
    ctx->gpr[VKMT_RSI] = ec->X25; ctx->gpr[VKMT_RDI] = ec->X26;
    ctx->gpr[VKMT_R8]  = ec->X2;  ctx->gpr[VKMT_R9]  = ec->X3;
    ctx->gpr[VKMT_R10] = ec->X4;  ctx->gpr[VKMT_R11] = ec->X5;
    ctx->gpr[VKMT_R12] = ec->X19; ctx->gpr[VKMT_R13] = ec->X20;
    ctx->gpr[VKMT_R14] = ec->X21; ctx->gpr[VKMT_R15] = ec->X22;
    ctx->rip    = ec->Pc;
    ctx->eflags = ec->AMD64_EFlags;
    ctx->mxcsr  = ec->AMD64_MxCsr;
    ctx->fcw    = ec->AMD64_ControlWord;
    memcpy( ctx->xmm, ec->V, sizeof(ctx->xmm) );
}

/**********************************************************************
 *           vkmt_context_to_ec
 */
void vkmt_context_to_ec( ARM64EC_NT_CONTEXT *ec, const struct vkmt_x64_context *ctx )
{
    ec->X8  = ctx->gpr[VKMT_RAX]; ec->X0  = ctx->gpr[VKMT_RCX];
    ec->X1  = ctx->gpr[VKMT_RDX]; ec->X27 = ctx->gpr[VKMT_RBX];
    ec->Sp  = ctx->gpr[VKMT_RSP]; ec->Fp  = ctx->gpr[VKMT_RBP];
    ec->X25 = ctx->gpr[VKMT_RSI]; ec->X26 = ctx->gpr[VKMT_RDI];
    ec->X2  = ctx->gpr[VKMT_R8];  ec->X3  = ctx->gpr[VKMT_R9];
    ec->X4  = ctx->gpr[VKMT_R10]; ec->X5  = ctx->gpr[VKMT_R11];
    ec->X19 = ctx->gpr[VKMT_R12]; ec->X20 = ctx->gpr[VKMT_R13];
    ec->X21 = ctx->gpr[VKMT_R14]; ec->X22 = ctx->gpr[VKMT_R15];
    ec->Pc           = ctx->rip;
    ec->AMD64_EFlags = ctx->eflags;
    ec->AMD64_MxCsr  = ec->AMD64_MxCsr_copy = ctx->mxcsr;
    ec->AMD64_ControlWord = ctx->fcw;
    memcpy( ec->V, ctx->xmm, sizeof(ctx->xmm) );
}

/**********************************************************************
 *           vkmt_thread_context
 */
struct vkmt_x64_context *vkmt_thread_context(void)
{
    CHPE_V2_CPU_AREA_INFO *cpu_area = vkmt_cpu_area();

    if (!cpu_area) return NULL;
    if (!cpu_area->EmulatorData[0])
    {
        struct vkmt_x64_context *ctx = RtlAllocateHeap( GetProcessHeap(), HEAP_ZERO_MEMORY,
                                                        sizeof(struct vkmt_x64_context) );
        if (ctx)
        {
            ctx->gsbase = (ULONGLONG)NtCurrentTeb();  /* x64 TLS/TEB access via gs: */
            ctx->fcw    = 0x27f;
            ctx->mxcsr  = 0x1f80;
        }
        cpu_area->EmulatorData[0] = ctx;
    }
    return cpu_area->EmulatorData[0];
}

/**********************************************************************
 *           vkmt_process_init
 */
NTSTATUS vkmt_process_init(void)
{
    TRACE( "VKMT x86-64 emulator skeleton (M5): process init\n" );
    return vkmt_jit_process_init();
}

/**********************************************************************
 *           vkmt_process_term
 */
void vkmt_process_term( HANDLE handle, BOOL is_post, NTSTATUS status )
{
    TRACE( "process term handle %p is_post %d status %lx\n", handle, is_post, status );
    if (is_post) vkmt_jit_process_term();
}

/**********************************************************************
 *           vkmt_thread_init
 */
NTSTATUS vkmt_thread_init(void)
{
    CHPE_V2_CPU_AREA_INFO *cpu_area = vkmt_cpu_area();

    TRACE( "thread %04lx init, cpu area %p\n",
                     GetCurrentThreadId(), cpu_area );
    if (!cpu_area)
    {
        ERR( "thread %04lx has no CHPE cpu area\n", GetCurrentThreadId() );
        return STATUS_UNSUCCESSFUL;
    }
    if (!vkmt_thread_context())
    {
        ERR( "failed to allocate guest context\n" );
        return STATUS_NO_MEMORY;
    }
    return STATUS_SUCCESS;
}

/**********************************************************************
 *           vkmt_thread_term
 */
void vkmt_thread_term( HANDLE handle, LONG exit_code )
{
    CHPE_V2_CPU_AREA_INFO *cpu_area = vkmt_cpu_area();

    TRACE( "thread term handle %p exit_code %lx\n", handle, exit_code );
    if (cpu_area && cpu_area->EmulatorData[0])
    {
        struct vkmt_x64_context *ctx = cpu_area->EmulatorData[0];

        TRACE( "thread interpreted %I64u guest instructions total\n", ctx->insn_total );
        RtlFreeHeap( GetProcessHeap(), 0, ctx );
        cpu_area->EmulatorData[0] = NULL;
    }
}

/**********************************************************************
 *           vkmt_is_processor_feature_present
 *
 * Baseline implemented by the interpreter.  In particular, keep SSE4.x
 * clear until its three-byte opcode maps are implemented.
 */
BOOLEAN vkmt_is_processor_feature_present( UINT feature )
{
    static const ULONGLONG x86_features =
        (1ull << PF_COMPARE_EXCHANGE_DOUBLE) |
        (1ull << PF_MMX_INSTRUCTIONS_AVAILABLE) |
        (1ull << PF_XMMI_INSTRUCTIONS_AVAILABLE) |
        (1ull << PF_RDTSC_INSTRUCTION_AVAILABLE) |
        (1ull << PF_XMMI64_INSTRUCTIONS_AVAILABLE) |
        (1ull << PF_NX_ENABLED) |
        (1ull << PF_SSE3_INSTRUCTIONS_AVAILABLE) |
        (1ull << PF_COMPARE_EXCHANGE128) |
        (1ull << PF_FASTFAIL_AVAILABLE) |
        (1ull << PF_RDTSCP_INSTRUCTION_AVAILABLE) |
        (1ull << PF_SSSE3_INSTRUCTIONS_AVAILABLE);

    return feature < 64 && (x86_features & (1ull << feature));
}

/**********************************************************************
 *           vkmt_update_processor_information
 */
void vkmt_update_processor_information( SYSTEM_CPU_INFORMATION *info )
{
    /* cf. the x86_64 unix side: AMD64 architecture, family/model in
     * level/revision, no feature bits */
    info->ProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
    info->ProcessorLevel        = 21;   /* Zen 2 family */
    info->ProcessorRevision     = 1;
    info->ProcessorFeatureBits  = 0;
}
