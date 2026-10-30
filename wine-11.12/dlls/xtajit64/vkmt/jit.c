/*
 * VKMT x86-64 emulator — M5 tier-0 ARM64 block cache
 *
 * A block is a small straight-line sequence of register-only guest
 * instructions.  The generated ARM64 body follows AAPCS64, preserves x29/x30
 * and directly lowers architectural NOPs; operations with observable state
 * continue through the shared executor.
 * Unsupported instructions remain in the interpreter.  This establishes a
 * W^X code cache, block lookup, and safe SMC retirement without crossing an
 * ARM64EC ABI boundary from generated code.
 */

#include <stdarg.h>
#include <stddef.h>

#include "vkmt.h"

WINE_DEFAULT_DEBUG_CHANNEL(vkmtx64);

#define VKMT_JIT_MAX_INSNS 16

struct vkmt_jit_block
{
    struct vkmt_jit_block *next;
    struct vkmt_jit_block *fallthrough;
    ULONGLONG rip;
    ULONG epoch;
    UINT count;
    SIZE_T code_size;
    void *code;
    BYTE direct[VKMT_JIT_MAX_INSNS];
    struct vkmt_insn insn[VKMT_JIT_MAX_INSNS];
};

static RTL_CRITICAL_SECTION jit_cs;
static BOOL jit_ready;
static ULONG jit_epoch;
static struct vkmt_jit_block *jit_blocks;
static PVOID jit_build_base;
static SIZE_T jit_build_size;

static void emit32( BYTE **p, ULONG insn )
{
    memcpy( *p, &insn, sizeof(insn) );
    *p += sizeof(insn);
}

/* movz/movk xd, #imm16, lsl #(16 * hw) */
static void emit_imm64( BYTE **p, UINT reg, ULONGLONG value )
{
    UINT hw;

    emit32( p, 0xd2800000 | ((value & 0xffff) << 5) | reg );
    for (hw = 1; hw != 4; hw++)
        emit32( p, 0xf2800000 | (hw << 21) | (((value >> (16 * hw)) & 0xffff) << 5) | reg );
}

static void emit_ptr( BYTE **p, UINT reg, const void *ptr )
{
    emit_imm64( p, reg, (ULONG_PTR)ptr );
}

static void emit_store_rip( BYTE **p, ULONGLONG rip )
{
    UINT offset = offsetof(struct vkmt_x64_context, rip);

    emit_imm64( p, 16, rip );
    emit32( p, 0xf9000000 | ((offset / 8) << 10) | 16 );  /* str x16, [x0, #rip] */
}

static void emit_store_ctx_gpr( BYTE **p, UINT reg, UINT gpr )
{
    UINT offset = offsetof(struct vkmt_x64_context, gpr) + gpr * sizeof(ULONGLONG);

    /* Every guest GPR has a 64-bit slot.  A 32-bit x86 write must be
     * zero-extended before the full slot is stored; STR W would leave stale
     * upper bits in the context. */
    emit32( p, 0xf9000000 | ((offset / 8) << 10) | reg );
}

static void emit_load_ctx_gpr( BYTE **p, UINT reg, UINT gpr, BOOL is64 )
{
    UINT offset = offsetof(struct vkmt_x64_context, gpr) + gpr * sizeof(ULONGLONG);

    emit32( p, (is64 ? 0xf9400000 : 0xb9400000) | ((offset / (is64 ? 8 : 4)) << 10) | reg );
}

static BOOL emit_mem_load( BYTE **p, UINT dst, UINT base, INT disp, BOOL is64 )
{
    if (disp < -256 || disp > 255) return FALSE;
    emit32( p, (is64 ? 0xf8400000 : 0xb8400000) | (((UINT)disp & 0x1ff) << 12) |
            (base << 5) | dst ); /* ldur x/wdst, [xbase, #disp] */
    return TRUE;
}

static BOOL emit_mem_store( BYTE **p, UINT src, UINT base, INT disp, BOOL is64 )
{
    if (disp < -256 || disp > 255) return FALSE;
    emit32( p, (is64 ? 0xf8000000 : 0xb8000000) | (((UINT)disp & 0x1ff) << 12) |
            (base << 5) | src ); /* stur x/wsrc, [xbase, #disp] */
    return TRUE;
}

/* Decode just enough ModRM to lower [base + disp8/disp32] forms.  SIB and
 * RIP-relative addressing stay in the executor until their own lowering is
 * available. */
static BOOL jit_simple_mem( const struct vkmt_insn *insn, UINT *base, INT *disp )
{
    const BYTE *p = insn->start;
    UINT modrm, mod, rm;

    while (*p == 0x66 || *p == 0xf2 || *p == 0xf3 || *p == 0xf0 ||
           *p == 0x2e || *p == 0x36 || *p == 0x3e || *p == 0x26 ||
           *p == 0x64 || *p == 0x65 || *p == 0x67) p++;
    if ((*p & 0xf0) == 0x40) p++;
    if (*p++ == 0x0f) return FALSE;
    modrm = *p;
    mod = modrm >> 6;
    rm = modrm & 7;
    if (mod == 3 || rm == 4 || (!mod && rm == 5)) return FALSE;
    *base = rm | ((insn->rex & 1) << 3);
    if (mod == 0) *disp = 0;
    else if (mod == 1) *disp = (signed char)p[1];
    else memcpy( disp, p + 1, sizeof(*disp) );
    return *disp >= -256 && *disp <= 255;
}

/* Emit operations whose x86 effects have no dependency on EFLAGS.  x0 is the
 * context base and x16 is the only scratch register. */
static void emit_binop( BYTE **p, UINT base64, UINT base32, UINT dst, UINT lhs, UINT rhs, BOOL is64 )
{
    emit32( p, (is64 ? base64 : base32) | (rhs << 16) | (lhs << 5) | dst );
}

static BOOL jit_emit_direct( BYTE **p, const struct vkmt_insn *insn, BOOL discard_flags )
{
    UINT op = insn->opcode, dst, src;
    BOOL is64 = insn->rex & 8;

    if (op == 0x90 && !insn->rex)
    {
        emit_store_rip( p, (ULONG_PTR)(insn->start + insn->len) );
        return TRUE;
    }
    if (op >= 0xb8 && op <= 0xbf && !(insn->pref & 0x01))
    {
        dst = (op & 7) | ((insn->rex & 1) << 3);
        emit_imm64( p, 16, is64 ? insn->imm : (UINT)insn->imm );
        emit_store_ctx_gpr( p, 16, dst );
        emit_store_rip( p, (ULONG_PTR)(insn->start + insn->len) );
        return TRUE;
    }
    if ((op == 0x89 || op == 0x8b) && insn->has_modrm && insn->rm_is_reg && !(insn->pref & 0x01))
    {
        if (op == 0x89) { dst = insn->rm; src = insn->reg; }
        else { dst = insn->reg; src = insn->rm; }
        emit_load_ctx_gpr( p, 16, src, is64 );
        emit_store_ctx_gpr( p, 16, dst );
        emit_store_rip( p, (ULONG_PTR)(insn->start + insn->len) );
        return TRUE;
    }
    if ((op == 0x89 || op == 0x8b) && insn->has_modrm && !insn->rm_is_reg && !(insn->pref & 0x01))
    {
        INT disp;
        UINT base;

        if (!jit_simple_mem( insn, &base, &disp )) return FALSE;
        emit_load_ctx_gpr( p, 16, base, TRUE );
        if (op == 0x8b)
        {
            if (!emit_mem_load( p, 17, 16, disp, is64 )) return FALSE;
            emit_store_ctx_gpr( p, 17, insn->reg );
        }
        else
        {
            /* Only stack stores are direct: other writes can modify guest
             * code and must pass through the invalidation-aware executor. */
            if (base != VKMT_RSP && base != VKMT_RBP) return FALSE;
            emit_load_ctx_gpr( p, 17, insn->reg, is64 );
            if (!emit_mem_store( p, 17, 16, disp, is64 )) return FALSE;
        }
        emit_store_rip( p, (ULONG_PTR)(insn->start + insn->len) );
        return TRUE;
    }
    if (discard_flags && insn->has_modrm && insn->rm_is_reg && !(insn->pref & 0x01))
    {
        if (op == 0x31 || op == 0x33) /* xor r/m,r / r,r/m */
        {
            if (op == 0x31) { dst = insn->rm; src = insn->reg; }
            else { dst = insn->reg; src = insn->rm; }
            emit_load_ctx_gpr( p, 16, dst, is64 );
            emit_load_ctx_gpr( p, 17, src, is64 );
            emit_binop( p, 0xca000000, 0x4a000000, 16, 16, 17, is64 );
            emit_store_ctx_gpr( p, 16, dst );
            emit_store_rip( p, (ULONG_PTR)(insn->start + insn->len) );
            return TRUE;
        }
        if (op == 0x01 || op == 0x03)
        {
            if (op == 0x01) { dst = insn->rm; src = insn->reg; }
            else { dst = insn->reg; src = insn->rm; }
            emit_load_ctx_gpr( p, 16, dst, is64 );
            emit_load_ctx_gpr( p, 17, src, is64 );
            emit_binop( p, 0x8b000000, 0x0b000000, 16, 16, 17, is64 );
            emit_store_ctx_gpr( p, 16, dst );
            emit_store_rip( p, (ULONG_PTR)(insn->start + insn->len) );
            return TRUE;
        }
        if (op == 0x83 && (insn->reg & 7) == 0 && (UINT)insn->imm < 4096)
        {
            emit_load_ctx_gpr( p, 16, insn->rm, is64 );
            emit32( p, (is64 ? 0x91000000 : 0x11000000) | ((UINT)insn->imm << 10) | (16 << 5) | 16 );
            emit_store_ctx_gpr( p, 16, insn->rm );
            emit_store_rip( p, (ULONG_PTR)(insn->start + insn->len) );
            return TRUE;
        }
        if (op == 0xc1 && (insn->reg & 7) >= 4 && (insn->reg & 7) <= 7)
        {
            UINT base;
            switch (insn->reg & 7)
            {
            case 4: base = is64 ? 0x9ac02000 : 0x1ac02000; break; /* lslv */
            case 5: base = is64 ? 0x9ac02400 : 0x1ac02400; break; /* lsrv */
            case 7: base = is64 ? 0x9ac02800 : 0x1ac02800; break; /* asrv */
            default: return FALSE; /* rotate needs x86 CF semantics */
            }
            emit_load_ctx_gpr( p, 16, insn->rm, is64 );
            emit_imm64( p, 17, insn->imm & (is64 ? 63 : 31) );
            emit_binop( p, base, base, 16, 16, 17, TRUE );
            emit_store_ctx_gpr( p, 16, insn->rm );
            emit_store_rip( p, (ULONG_PTR)(insn->start + insn->len) );
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL jit_eligible( const struct vkmt_insn *insn )
{
    UINT op = insn->opcode;
    INT disp;
    UINT base;

    if (op == 0x90) return !insn->rex;  /* REX.B changes this into xchg */
    if (op >= 0xb8 && op <= 0xbf && !(insn->pref & 0x01)) return TRUE;
    if ((op == 0x89 || op == 0x8b) && insn->has_modrm && !insn->rm_is_reg &&
        !(insn->pref & 0x01) && jit_simple_mem( insn, &base, &disp ))
        return op == 0x8b || base == VKMT_RSP || base == VKMT_RBP;
    if (!insn->has_modrm || !insn->rm_is_reg || (op & 0x100)) return FALSE;
    /* Register-only ALU/move/shift forms have no captured effective address.
     * The shared executor retains the exact x86 flags and corner semantics. */
    return (op <= 0x3b && (op & 7) <= 3) || (op >= 0x80 && op <= 0x8b) ||
           op == 0x8d || (op >= 0xc0 && op <= 0xc1);
}

static BOOL jit_direct_eligible( const struct vkmt_insn *insn )
{
    UINT op = insn->opcode, base;
    INT disp;

    if (op == 0x90) return !insn->rex;
    if (op >= 0xb8 && op <= 0xbf && !(insn->pref & 0x01)) return TRUE;
    if ((op == 0x89 || op == 0x8b) && insn->has_modrm && insn->rm_is_reg && !(insn->pref & 0x01)) return TRUE;
    if ((op == 0x89 || op == 0x8b) && insn->has_modrm && !insn->rm_is_reg &&
        !(insn->pref & 0x01) && jit_simple_mem( insn, &base, &disp ))
        return op == 0x8b || base == VKMT_RSP || base == VKMT_RBP;
    return FALSE;
}

static BOOL jit_writes_flags( const struct vkmt_insn *insn )
{
    UINT op = insn->opcode;
    return (op <= 0x3b && (op & 7) <= 5) || (op >= 0x80 && op <= 0x85) ||
           op == 0xc0 || op == 0xc1;
}

static BOOL jit_reads_flags( const struct vkmt_insn *insn )
{
    UINT op = insn->opcode;
    return (op >= 0x10 && op <= 0x1b && (op & 7) <= 3) || op == 0x70 ||
           (op >= 0x70 && op <= 0x7f) || op == 0xe0 || op == 0xe1 || op == 0xe2;
}

static struct vkmt_jit_block *jit_find_block( ULONGLONG rip )
{
    struct vkmt_jit_block *block;

    for (block = jit_blocks; block; block = block->next)
        if (block->epoch == jit_epoch && block->rip == rip) return block;
    return NULL;
}

static struct vkmt_jit_block *jit_compile( struct vkmt_x64_context *ctx )
{
    struct vkmt_jit_block *block;
    BYTE *code, *p;
    PVOID base;
    SIZE_T size, protect_size;
    ULONG old_prot;
    MEM_EXTENDED_PARAMETER param = { 0 };
    UINT i;
    BOOL flags_dead = FALSE;

    block = RtlAllocateHeap( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*block) );
    if (!block) return NULL;
    block->rip = ctx->rip;
    for (i = 0; i < VKMT_JIT_MAX_INSNS; i++)
    {
        struct vkmt_x64_context probe = *ctx;

        probe.rip = block->rip;
        if (i) probe.rip = (ULONG_PTR)(block->insn[i - 1].start + block->insn[i - 1].len);
        vkmt_decode( &probe, &block->insn[i] );
        if (!jit_eligible( &block->insn[i] )) break;
        block->count++;
    }
    if (!block->count) goto fail;

    for (i = block->count; i-- > 0; )
    {
        block->direct[i] = jit_direct_eligible( &block->insn[i] );
        if (jit_writes_flags( &block->insn[i] ))
        {
            if (flags_dead) block->direct[i] = TRUE;
            flags_dead = TRUE;
        }
        if (jit_reads_flags( &block->insn[i] )) flags_dead = FALSE;
    }

    block->code_size = 16 + block->count * 64 + 8;
    size = (block->code_size + 0xfff) & ~0xfffull;
    base = NULL;
    /* Mark this as EC code.  On an ARM64EC process, the ntdll signal layer
     * otherwise treats a dynamically allocated executable address as x64
     * guest code and routes it back to the emulator instead of executing the
     * ARM64 instructions we emitted. */
    param.Type = MemExtendedParameterAttributeFlags;
    param.ULong64 = MEM_EXTENDED_PARAMETER_EC_CODE;
    if (NtAllocateVirtualMemoryEx( GetCurrentProcess(), &base, &size,
                                   MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, &param, 1 )) goto fail;
    jit_build_base = base;
    jit_build_size = size;
    code = p = base;
    emit32( &p, 0xa9be7bfd );             /* stp x29, x30, [sp, #-32]! */
    emit32( &p, 0x910003fd );             /* mov x29, sp */
    emit32( &p, 0xf9000be0 );             /* str x0, [sp, #16] */
    for (i = 0; i < block->count; i++)
    {
        /* C fallback calls may clobber x0, so every lowered instruction
         * reloads the context from our ABI-owned stack slot. */
        emit32( &p, 0xf9400be0 );         /* ldr x0, [sp, #16] */
        if (block->direct[i] && jit_emit_direct( &p, &block->insn[i], jit_writes_flags( &block->insn[i] ) ))
            continue;
        emit32( &p, 0xf9400be0 );         /* ldr x0, [sp, #16] */
        emit_ptr( &p, 1, &block->insn[i] );
        emit_ptr( &p, 16, vkmt_execute_decoded );
        emit32( &p, 0xd63f0200 );         /* blr x16 */
    }
    emit32( &p, 0xa8c27bfd );             /* ldp x29, x30, [sp], #32 */
    emit32( &p, 0xd65f03c0 );             /* ret */
    NtFlushInstructionCache( GetCurrentProcess(), code, p - code );
    protect_size = size;
    if (NtProtectVirtualMemory( GetCurrentProcess(), &base, &protect_size,
                                PAGE_EXECUTE_READ, &old_prot ))
    {
        NtFreeVirtualMemory( GetCurrentProcess(), &base, &size, MEM_RELEASE );
        goto fail;
    }
    block->code = code;
    jit_build_base = NULL;
    jit_build_size = 0;
    {
        UINT direct_count = 0;
        for (i = 0; i < block->count; i++) direct_count += block->direct[i];
        TRACE( "JIT block %p: %u guest insns (%u direct) -> %p (%Iu bytes)\n",
               (void *)block->rip, block->count, direct_count, code, block->code_size );
    }
    return block;

fail:
    jit_build_base = NULL;
    jit_build_size = 0;
    RtlFreeHeap( GetProcessHeap(), 0, block );
    return NULL;
}

NTSTATUS vkmt_jit_process_init(void)
{
    if (!jit_ready)
    {
        RtlInitializeCriticalSection( &jit_cs );
        jit_epoch = 1;
        jit_ready = TRUE;
    }
    return STATUS_SUCCESS;
}

void vkmt_jit_process_term(void)
{
    struct vkmt_jit_block *block, *next;

    if (!jit_ready) return;
    RtlEnterCriticalSection( &jit_cs );
    for (block = jit_blocks; block; block = next)
    {
        PVOID base = block->code;
        SIZE_T size = 0;
        next = block->next;
        if (base) NtFreeVirtualMemory( GetCurrentProcess(), &base, &size, MEM_RELEASE );
        RtlFreeHeap( GetProcessHeap(), 0, block );
    }
    jit_blocks = NULL;
    RtlLeaveCriticalSection( &jit_cs );
}

void vkmt_jit_invalidate( void *addr, SIZE_T size )
{
    if (!jit_ready || !addr || !size) return;
    if (jit_build_base && (BYTE *)addr < (BYTE *)jit_build_base + jit_build_size &&
        (BYTE *)addr + size > (BYTE *)jit_build_base) return;
    /* Retire, rather than free, blocks: another guest thread may currently
     * be in one.  The post-invalidation lookup can never select its epoch. */
    RtlEnterCriticalSection( &jit_cs );
    jit_epoch++;
    if (!jit_epoch) jit_epoch = 1;
    RtlLeaveCriticalSection( &jit_cs );
    TRACE( "JIT retired blocks after guest code change %p +%Ix (epoch %lu)\n", addr, size, jit_epoch );
}

UINT vkmt_jit_run_block( struct vkmt_x64_context *ctx )
{
    struct vkmt_jit_block *block, *linked = NULL;
    void (WINAPI *run)(struct vkmt_x64_context *);
    UINT total = 0;

    if (!jit_ready || !ctx->rip) return 0;
    for (;;)
    {
        RtlEnterCriticalSection( &jit_cs );
        if (linked && linked->epoch == jit_epoch && linked->rip == ctx->rip)
            block = linked;
        else block = jit_find_block( ctx->rip );
        if (!block)
        {
            block = jit_compile( ctx );
            if (block)
            {
                block->epoch = jit_epoch;
                block->next = jit_blocks;
                jit_blocks = block;
            }
        }
        RtlLeaveCriticalSection( &jit_cs );
        if (!block) break;
        run = (void *)block->code;
        run( ctx );
        total += block->count;
        RtlEnterCriticalSection( &jit_cs );
        if (block->epoch == jit_epoch)
        {
            if (!block->fallthrough || block->fallthrough->epoch != jit_epoch ||
                block->fallthrough->rip != ctx->rip)
                block->fallthrough = jit_find_block( ctx->rip );
            linked = block->fallthrough;
        }
        else linked = NULL;
        RtlLeaveCriticalSection( &jit_cs );
        /* A block contains only linear instructions.  Follow its cached
         * fall-through rather than returning to decode between blocks; bound
         * a chain for fairness. */
        if (total >= VKMT_JIT_MAX_INSNS * 4) break;
    }
    return total;
}
