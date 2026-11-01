# Wine 11.12 files changed by FEX for Mac

This directory contains only the files of **Wine 11.12** (https://www.winehq.org/) that FEX for Mac
modified or added. Everything is licensed under the **LGPL-2.1-or-later** like Wine itself.
To rebuild: unpack Wine 11.12, copy these files over it, then follow `scripts/`.

Main themes: macOS arm64 + 16 KiB pages, the "guest window" address layout for 32-bit programs
(`include/wine/wow_guest.h`, wow64 and the unix libraries that translate 32-bit pointers), TEB in x28,
fixes for fonts (fontconfig fallback), Direct3D/OpenGL/Vulkan/DirectWrite/audio thunks, and the minimal
experimental x86-64 interpreter in `dlls/xtajit64/`.

## Files
- `configure.ac`
- `dlls/crypt32/unixlib.c`
- `dlls/ctapi32/unixlib.c`
- `dlls/dnsapi/libresolv.c`
- `dlls/dwrite/freetype.c`
- `dlls/kerberos/unixlib.c`
- `dlls/kernel32/process.c`
- `dlls/kernel32/sync.c`
- `dlls/kernelbase/debug.c`
- `dlls/kernelbase/sync.c`
- `dlls/msv1_0/unixlib.c`
- `dlls/netapi32/unixlib.c`
- `dlls/ntdll/exception.c`
- `dlls/ntdll/loader.c`
- `dlls/ntdll/ntdll.spec`
- `dlls/ntdll/process.c`
- `dlls/ntdll/signal_arm64.c`
- `dlls/ntdll/signal_arm64ec.c`
- `dlls/ntdll/thread.c`
- `dlls/ntdll/unix/debug.c`
- `dlls/ntdll/unix/env.c`
- `dlls/ntdll/unix/process.c`
- `dlls/ntdll/unix/server.c`
- `dlls/ntdll/unix/signal_arm64.c`
- `dlls/ntdll/unix/socket.c`
- `dlls/ntdll/unix/sync.c`
- `dlls/ntdll/unix/system.c`
- `dlls/ntdll/unix/thread.c`
- `dlls/ntdll/unix/unix_private.h`
- `dlls/ntdll/unix/virtual.c`
- `dlls/ntoskrnl.exe/instr.c`
- `dlls/opengl32/unix_private.h`
- `dlls/opengl32/unix_thunks.c`
- `dlls/opengl32/unix_wgl.c`
- `dlls/secur32/schannel_gnutls.c`
- `dlls/win32u/dib.c`
- `dlls/win32u/font.c`
- `dlls/win32u/freetype.c`
- `dlls/win32u/gdiobj.c`
- `dlls/win32u/message.c`
- `dlls/winecoreaudio.drv/coreaudio.c`
- `dlls/wined3d/adapter_gl.c`
- `dlls/wined3d/glsl_shader.c`
- `dlls/winemac.drv/keyboard.c`
- `dlls/winemac.drv/macdrv_cocoa.h`
- `dlls/winemac.drv/macdrv_main.c`
- `dlls/winemac.drv/opengl.c`
- `dlls/winevulkan/vulkan_private.h`
- `dlls/winscard/unixlib.c`
- `dlls/winspool.drv/cups.c`
- `dlls/wow64/process.c`
- `dlls/wow64/security.c`
- `dlls/wow64/sync.c`
- `dlls/wow64/syscall.c`
- `dlls/wow64/system.c`
- `dlls/wow64/virtual.c`
- `dlls/wow64/wow64_private.h`
- `dlls/wow64win/user.c`
- `dlls/wow64win/wow64win_private.h`
- `dlls/ws2_32/protocol.c`
- `dlls/ws2_32/unixlib.c`
- `dlls/xtajit64/Makefile.in`
- `dlls/xtajit64/cpu.c`
- `dlls/xtajit64/vkmt/context.c`
- `dlls/xtajit64/vkmt/dispatch.c`
- `dlls/xtajit64/vkmt/interp.c`
- `dlls/xtajit64/vkmt/jit.c`
- `dlls/xtajit64/vkmt/vkmt.h`
- `include/wine/wow_guest.h`
- `include/winnt.h`
- `include/winternl.h`
- `loader/main.c`
- `loader/wine.inf.in`
- `server/fd.c`
- `tools/makedep.c`
- `tools/vkmt-fix-x18-all.py`
- `tools/vkmt-fix-x18-tls.py`

## macOS ARM64 CPU ID registers (Oct 31 2026)

`dlls/ntdll/unix/system.c`: `get_core_id_regs_arm64()` was a stub off Linux, so the
`HKLM\Hardware\Description\System\CentralProcessor\N\CP 40xx` values were never created and FEX
(which reads them) assumed a baseline ARMv8.0 CPU with no LSE atomics, LRCPC or FlagM. On macOS the
ID_AA64* fields are now rebuilt from the `hw.optional.arm.FEAT_*` sysctls.
Patch: `patches/wine-macos-arm64-id-regs.patch`.

## WoW64 OpenGL: explicit-flush maps copy only flushed ranges (Nov 1 2026)

`dlls/opengl32/unix_wgl.c`: a 32-bit program's buffer map that can't be returned directly (the driver's pointer
is above 4 GB) goes through a copy. For write-only maps with `GL_MAP_FLUSH_EXPLICIT_BIT` (wined3d's dynamic buffer
updates) the whole-buffer copy in on map and copy back on unmap are replaced by copying each flushed range at
`glFlushMappedBufferRange` time, before the driver flush. IWBTGR: 1.20 -> 0.74 CPU cores at 50 fps.
Patch: `patches/wine-wow64-gl-flush-explicit-copy.patch`.

## Mac driver: per-program KeyRemap (Nov 1 2026)

`dlls/winemac.drv`: new `KeyRemap` option (`HKCU\Software\Wine\Mac Driver` or
`HKCU\Software\Wine\AppDefaults\<exe>\Mac Driver`), e.g. `Space=Shift, Z=J`: the key on the left acts as the key on
the right. Applied to the Mac keycode before it is translated. The app's Game > Key Remapping menu edits it.
Patch: `patches/wine-macdrv-keyremap.patch`.
