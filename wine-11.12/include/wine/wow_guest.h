/*
 * Guest-window support for WoW64 unix libraries (macOS arm64).
 *
 * A 32-bit pointer is an offset into the window that starts at the host address reported by
 * ProcessWineIosWowGuestBase (0 = identity mapping). Including this header makes ULongToPtr()
 * and PtrToUlong() translate accordingly, unless WOW_GUEST_NO_REDIRECT is defined first (for
 * libraries where some of those conversions are not guest pointers); then use wow_to_host()
 * and wow_to_guest() explicitly.
 */
#ifndef __WINE_WOW_GUEST_H
#define __WINE_WOW_GUEST_H

#ifdef __APPLE__

static inline ULONG_PTR wow_guest_base(void)
{
    static ULONG_PTR base;
    static int init;
    if (!init)
    {
        if (NtQueryInformationProcess( (HANDLE)~(ULONG_PTR)0, ProcessWineIosWowGuestBase, &base, sizeof(base), NULL ))
            base = 0;
        init = 1;
    }
    return base;
}

static inline void *wow_to_host( ULONG addr )
{
    return addr ? (void *)(wow_guest_base() + addr) : NULL;
}

static inline ULONG wow_to_guest( const void *ptr )
{
    ULONG_PTR addr = (ULONG_PTR)ptr, base = wow_guest_base();
    if (base && addr >= base && addr - base < ((ULONG_PTR)1 << 32)) return (ULONG)(addr - base);
    return (ULONG)addr;
}

#ifndef WOW_GUEST_NO_REDIRECT
#undef ULongToPtr
#undef UlongToPtr
#undef PtrToUlong
#define ULongToPtr(x) wow_to_host(x)
#define UlongToPtr(x) wow_to_host(x)
#define PtrToUlong(x) wow_to_guest(x)
#endif

#endif /* __APPLE__ */
#endif /* __WINE_WOW_GUEST_H */
