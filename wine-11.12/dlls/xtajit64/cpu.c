/*
 * x86-64 emulation on ARM64 — VKMT emulator skeleton (M2)
 *
 * Copyright 2024 Alexandre Julliard
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
 * Thin xtajit64.dll export layer; the implementation lives in vkmt/.
 * The control-flow entries (BeginSimulation/DispatchJump/ExitToX64/
 * RetToEntryThunk) are naked asm routines exported directly from
 * vkmt/dispatch.c.
 */

#include <stdarg.h>

#include "ntstatus.h"
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winternl.h"
#include "wine/debug.h"

#include "vkmt/vkmt.h"

WINE_DEFAULT_DEBUG_CHANNEL(vkmtx64);


/**********************************************************************
 *           BTCpu64FlushInstructionCache  (xtajit64.@)
 */
void WINAPI BTCpu64FlushInstructionCache( void *addr, SIZE_T size )
{
    TRACE( "%p %Ix\n", addr, size );
    vkmt_jit_invalidate( addr, size );
}


/**********************************************************************
 *           BTCpu64IsProcessorFeaturePresent  (xtajit64.@)
 */
BOOLEAN WINAPI BTCpu64IsProcessorFeaturePresent( UINT feature )
{
    return vkmt_is_processor_feature_present( feature );
}


/**********************************************************************
 *           BTCpu64NotifyMemoryDirty  (xtajit64.@)
 */
void WINAPI BTCpu64NotifyMemoryDirty( void *addr, SIZE_T size )
{
    TRACE( "%p %Ix\n", addr, size );
    vkmt_jit_invalidate( addr, size );
}


/**********************************************************************
 *           BTCpu64NotifyReadFile  (xtajit64.@)
 */
void WINAPI BTCpu64NotifyReadFile( HANDLE handle, void *addr, SIZE_T size, BOOL is_post, NTSTATUS status )
{
    TRACE( "%p %p %Ix is_post %d status %lx\n", handle, addr, size, is_post, status );
}


/**********************************************************************
 *           FlushInstructionCacheHeavy  (xtajit64.@)
 */
void WINAPI FlushInstructionCacheHeavy( void *addr, SIZE_T size )
{
    TRACE( "%p %Ix\n", addr, size );
    vkmt_jit_invalidate( addr, size );
}


/**********************************************************************
 *           NotifyMapViewOfSection  (xtajit64.@)
 */
NTSTATUS WINAPI NotifyMapViewOfSection( void *unk1, void *addr, void *unk2, SIZE_T size,
                                        ULONG alloc_type, ULONG protect )
{
    TRACE( "%p %Ix %lx %lx\n", addr, size, alloc_type, protect );
    return STATUS_SUCCESS;
}


/**********************************************************************
 *           NotifyMemoryAlloc  (xtajit64.@)
 */
void WINAPI NotifyMemoryAlloc( void *addr, SIZE_T size, ULONG type, ULONG prot, BOOL is_post, NTSTATUS status )
{
    TRACE( "%p %Ix type %lx prot %lx is_post %d status %lx\n", addr, size, type, prot, is_post, status );
}


/**********************************************************************
 *           NotifyMemoryFree  (xtajit64.@)
 */
void WINAPI NotifyMemoryFree( void *addr, SIZE_T size, ULONG type, BOOL is_post, NTSTATUS status )
{
    TRACE( "%p %Ix type %lx is_post %d status %lx\n", addr, size, type, is_post, status );
}


/**********************************************************************
 *           NotifyMemoryProtect  (xtajit64.@)
 */
void WINAPI NotifyMemoryProtect( void *addr, SIZE_T size, ULONG prot, BOOL is_post, NTSTATUS status )
{
    TRACE( "%p %Ix prot %lx is_post %d status %lx\n", addr, size, prot, is_post, status );
    if (is_post && !status) vkmt_jit_invalidate( addr, size );
}


/**********************************************************************
 *           NotifyUnmapViewOfSection  (xtajit64.@)
 */
void WINAPI NotifyUnmapViewOfSection( void *addr, BOOL is_post, NTSTATUS status )
{
    TRACE( "%p is_post %d status %lx\n", addr, is_post, status );
}


/**********************************************************************
 *           ProcessInit  (xtajit64.@)
 */
NTSTATUS WINAPI ProcessInit(void)
{
    return vkmt_process_init();
}


/**********************************************************************
 *           ProcessTerm  (xtajit64.@)
 */
void WINAPI ProcessTerm( HANDLE handle, BOOL is_post, NTSTATUS status )
{
    vkmt_process_term( handle, is_post, status );
}


/**********************************************************************
 *           ResetToConsistentState  (xtajit64.@)
 */
void WINAPI ResetToConsistentState( EXCEPTION_RECORD *rec, CONTEXT *context, ARM64_NT_CONTEXT *arm_ctx )
{
    vkmt_reset_to_consistent_state( rec, context, arm_ctx );
}


/**********************************************************************
 *           ThreadInit  (xtajit64.@)
 */
NTSTATUS WINAPI ThreadInit(void)
{
    return vkmt_thread_init();
}


/**********************************************************************
 *           ThreadTerm  (xtajit64.@)
 */
void WINAPI ThreadTerm( HANDLE handle, LONG exit_code )
{
    vkmt_thread_term( handle, exit_code );
}


/**********************************************************************
 *           UpdateProcessorInformation  (xtajit64.@)
 */
void WINAPI UpdateProcessorInformation( SYSTEM_CPU_INFORMATION *info )
{
    vkmt_update_processor_information( info );
}


/**********************************************************************
 *           DllMain
 */
BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH) LdrDisableThreadCalloutsForDll( inst );
    return TRUE;
}
