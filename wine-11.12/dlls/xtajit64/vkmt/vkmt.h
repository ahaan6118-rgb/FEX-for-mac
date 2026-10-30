/*
 * VKMT x86-64 emulator skeleton — private shared header
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

#ifndef __VKMT_X64EMU_H
#define __VKMT_X64EMU_H

#include "ntstatus.h"
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winternl.h"
#include "wine/debug.h"

/* debug channel: each .c file sets WINE_DEFAULT_DEBUG_CHANNEL(vkmtx64) */

/* Private per-thread guest x86-64 context.  The live register file during
 * simulation is the cpu area's ContextAmd64 (that is what ntdll fills and
 * what NtContinue consumes); this block is the emulator's own copy, used by
 * the interpreter and extended by M3 (decode caches, flags state, ...).
 * Linked from CHPE_V2_CPU_AREA_INFO.EmulatorData[0]. */
enum vkmt_gpr
{
    VKMT_RAX, VKMT_RCX, VKMT_RDX, VKMT_RBX,
    VKMT_RSP, VKMT_RBP, VKMT_RSI, VKMT_RDI,
    VKMT_R8,  VKMT_R9,  VKMT_R10, VKMT_R11,
    VKMT_R12, VKMT_R13, VKMT_R14, VKMT_R15
};

struct vkmt_x64_context
{
    /* Kept first so ntdll can access the KUED bridge without depending on
     * the interpreter's private register-file layout. */
    ARM64EC_NT_CONTEXT *exception_dispatch_context;
    ULONGLONG gpr[16];      /* x86 encoding order: rax rcx rdx rbx rsp rbp rsi rdi r8-r15 */
    ULONGLONG rip;
    ULONG     eflags;
    ULONG     mxcsr;
    USHORT    fcw;
    USHORT    fsw;
    ULONGLONG fsbase, gsbase;
    M128A     xmm[16];
    /* interpreter diagnostics */
    ULONGLONG branch_ring[16];  /* last 16 branch targets */
    UINT      branch_pos;
    ULONGLONG insn_total;       /* lifetime instructions interpreted */
    /* execution ring: last 8192 executed instructions (rip, eflags, rax, rcx) */
    ULONGLONG exec_ring[8192][4];
    UINT      exec_pos;
    /* native lr captured at ExitToX64/DispatchJump entry (the exit thunk's
     * return address); EXIT_RETURN resumes here — ec->Lr cannot be used for
     * this because the RetToEntryThunk capture overwrites it with the
     * planted marker on EC-callee returns */
    ULONGLONG native_ret;
    /* ExitToX64 thunk-entry frame stack (thunk sp, fp, native lr): nested
     * EC->guest calls (exception dispatch calls several guest handlers)
     * must each restore their own frame on EXIT_RETURN */
    ULONGLONG exit_sp[64];
    ULONGLONG exit_fp[64];
    ULONGLONG exit_lr[64];
    UINT      exit_depth;
    /* x64->EC call return LIFO: EXIT_NATIVE (guest call into EC code) has no
     * thunk to convert the ABI, so the emulator repacks stack args itself
     * (arm64 callee reads them at [sp], x64 caller put them at [rsp+0x20])
     * and saves the guest return address / post-call rsp here; the
     * EcCallRet trampoline restores them when the EC callee returns. */
    ULONGLONG ec_ret_rip[64];
    ULONGLONG ec_ret_rsp[64];
    UINT      ec_ret_depth;
};

/* context.c */
NTSTATUS vkmt_process_init(void);
void     vkmt_process_term( HANDLE handle, BOOL is_post, NTSTATUS status );
NTSTATUS vkmt_thread_init(void);
void     vkmt_thread_term( HANDLE handle, LONG exit_code );
void     vkmt_update_processor_information( SYSTEM_CPU_INFORMATION *info );
BOOLEAN  vkmt_is_processor_feature_present( UINT feature );

struct vkmt_x64_context *vkmt_thread_context(void);
void vkmt_context_from_ec( struct vkmt_x64_context *ctx, const ARM64EC_NT_CONTEXT *ec );
void vkmt_context_to_ec( ARM64EC_NT_CONTEXT *ec, const struct vkmt_x64_context *ctx );

/* dispatch.c */
void vkmt_begin_simulation(void);       /* C body for BeginSimulation */
void     vkmt_simulate_from_ec( int plant_marker ); /* C body for ExitToX64/DispatchJump/RetToEntryThunk */
void vkmt_reset_to_consistent_state( EXCEPTION_RECORD *rec, CONTEXT *context, ARM64_NT_CONTEXT *arm_ctx );

/* Decoder metadata is deliberately independent of the execution tier.  M5
 * caches only register-only instances: memory EAs are resolved by decode and
 * therefore must never be reused after the guest registers have changed. */
struct vkmt_insn
{
    const BYTE *start;
    UINT len, opcode, rex, pref;
    BOOL has_modrm;
    UINT reg, rm;
    BOOL rm_is_reg;
    ULONGLONG ea, imm;
    UINT imm_size;
    BOOL bad_ea;
};

/* interp.c — interpreter result codes */
enum vkmt_interp_result
{
    VKMT_INTERP_EXIT_NATIVE,   /* guest RIP reached native (EC) code; exit target in rip */
    VKMT_INTERP_EXIT_RETURN,   /* guest returned to the RetToEntryThunk marker */
    VKMT_INTERP_GIVEUP         /* cannot continue (unsupported opcode / null rip) */
};

struct vkmt_interp_outcome
{
    enum vkmt_interp_result result;
    ULONGLONG target;          /* EXIT_NATIVE: native pc */
    ULONG     opcode;          /* GIVEUP: offending opcode byte(s) */
};

void vkmt_interp_run( struct vkmt_x64_context *ctx, struct vkmt_interp_outcome *out );
void vkmt_decode( struct vkmt_x64_context *ctx, struct vkmt_insn *insn );
BOOL vkmt_execute_decoded( struct vkmt_x64_context *ctx, const struct vkmt_insn *insn );
BOOL vkmt_execute_decoded_block( struct vkmt_x64_context *ctx, const struct vkmt_insn *insn, UINT count );

/* jit.c — tier-0 block cache and its memory-coherency notifications */
NTSTATUS vkmt_jit_process_init(void);
void vkmt_jit_process_term(void);
UINT vkmt_jit_run_block( struct vkmt_x64_context *ctx );
void vkmt_jit_invalidate( void *addr, SIZE_T size );

/* interp.c — full guest dump (giveup report); used by the exception path too */
void vkmt_dump_guest( struct vkmt_x64_context *ctx, UINT opcode, const char *why );

#endif /* __VKMT_X64EMU_H */
