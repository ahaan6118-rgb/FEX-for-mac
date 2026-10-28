# Credits

FEX for Mac is built on the work of several open-source projects and communities.

## Core Components

### Wine (winehq.org)
Windows compatibility layer that implements the Windows API on Unix-like systems.
Licensed under LGPL 2.1.
https://www.winehq.org/

### FEX-Emu (fex-emu.com)
x86 and x86-64 JIT translator for ARM64. Provides the `xtajit.dll` WoW64 provider
that translates i386 Windows code to ARM64 at runtime.
Licensed under MIT.
https://github.com/FEX-Emu/FEX

### willfaust's FEX and Wine forks (Guest Window)
The 32-bit (i386) support uses the "Guest Window" approach from willfaust's forks of FEX
and Wine, which let a 32-bit guest run at a non-zero host base address on macOS arm64.
FEX for Mac carries additional local modifications to this FEX fork (for example moving
the JIT state register off x28 so Wine can keep the TEB there). FEX is MIT licensed.
https://github.com/willfaust/FEX
https://github.com/willfaust/wine

### GnuTLS and its dependencies
Wine's schannel and PFX support load GnuTLS at runtime. FEX for Mac bundles GnuTLS with
nettle, libtasn1, p11-kit, libidn2, libunistring, gmp and gettext (libintl) from Homebrew
builds. These are licensed under LGPL 2.1+ (GnuTLS, nettle) and related free licenses.
https://www.gnutls.org/

### MetalSharp / VKMT-Wine
Pioneered running Wine on ARM64 macOS without Rosetta 2. Their work includes:
- Wine patches for ARM64 macOS (KUSER_SHARED_DATA relocation, address space layout)
- WoW64 provider DLL architecture for FEX integration
- x18 to x28 TEB register fix for macOS ARM64 (macOS reserves x18)
- The xtajit64.dll x86-64 interpreter
https://github.com/MetalSharp

### DXMT
D3D11 to Metal translation layer, enabling DirectX 11 games to render via Apple's
Metal graphics API.

### MoltenVK (moltengl.com)
Vulkan to Metal translation layer by The Brenwill Workshop.
Licensed under Apache 2.0.
https://github.com/KhronosGroup/MoltenVK

### mkxp-z
Open-source reimplementation of the RPG Maker XP/VX/VX Ace runtime, enabling
RPG Maker games to run natively on ARM64 macOS without Wine.
https://github.com/mkxp-z/mkxp-z

### llvm-mingw
MinGW toolchain based on LLVM, used for cross-compiling Windows PE binaries
on macOS (ARM64EC, x86-64, i386 targets).
https://github.com/mstorsjo/llvm-mingw

## Thank You

This project would not be possible without the open-source community. If you're
using FEX for Mac, please consider supporting these upstream projects.
