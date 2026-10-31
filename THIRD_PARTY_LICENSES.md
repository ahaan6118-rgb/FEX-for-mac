# Third-Party Licenses

FEX for Mac bundles and uses the following open-source projects.
Each component retains its original license.

---

## FEXCore (FEX-Emu)

- **Project**: https://github.com/FEX-Emu/FEX
- **License**: MIT
- **Usage**: x86_64 → ARM64 JIT translation engine (libfexbridge.dylib)
- **Copyright**: Copyright (c) 2020-2026 FEX-Emu maintainers

## Wine

- **Project**: https://www.winehq.org/
- **License**: GNU Lesser General Public License v2.1 (LGPL-2.1)
- **Usage**: Windows compatibility layer (wine binary + DLLs)
- **Copyright**: Copyright (c) 1993-2026 the Wine project authors

## DXMT

- **Project**: https://github.com/3Shain/dxmt
- **License**: GNU Lesser General Public License v2.1 (LGPL-2.1)
- **Usage**: D3D11 → Metal translation layer
- **Copyright**: Copyright (c) 2023-2026 Feifan He for CodeWeavers

## MoltenVK

- **Project**: https://github.com/KhronosGroup/MoltenVK
- **License**: Apache License 2.0
- **Usage**: Vulkan → Metal translation (libMoltenVK.dylib)
- **Copyright**: Copyright (c) 2015-2026 The Brenwill Workshop Ltd.

## FreeType

- **Project**: https://freetype.org/
- **License**: FreeType License (BSD-style, with attribution) or GPL-2.0 - your choice
- **Usage**: Font rendering for Wine (libfreetype.6.dylib)

## libpng

- **Project**: http://www.libpng.org/
- **License**: libpng License (zlib-style)
- **Usage**: Image decoding for Wine (libpng16.16.dylib)

## GnuTLS and dependencies

Loaded by Wine's schannel/PFX support (libgnutls.30.dylib and friends, from Homebrew builds):

- **GnuTLS** - LGPL-2.1-or-later - https://www.gnutls.org/
- **Nettle / Hogweed** - LGPL-3.0-or-later or GPL-2.0-or-later - https://www.lysator.liu.se/~nisse/nettle/
- **GMP** - LGPL-3.0-or-later or GPL-2.0-or-later - https://gmplib.org/
- **libtasn1** - LGPL-2.1-or-later - https://www.gnu.org/software/libtasn1/
- **p11-kit** - BSD-3-Clause - https://p11-glue.github.io/p11-glue/p11-kit.html
- **libidn2** - LGPL-3.0-or-later or GPL-2.0-or-later - https://gitlab.com/libidn/libidn2
- **libunistring** - LGPL-3.0-or-later or GPL-2.0-or-later - https://www.gnu.org/software/libunistring/
- **gettext (libintl)** - LGPL-2.1-or-later - https://www.gnu.org/software/gettext/

## Vulkan Loader

- **Project**: https://github.com/KhronosGroup/Vulkan-Loader
- **License**: Apache License 2.0
- **Usage**: libvulkan.1.dylib

## willfaust's FEX and Wine forks ("Guest Window")

- **Projects**: https://github.com/willfaust/FEX , https://github.com/willfaust/wine
- **License**: MIT (FEX) / LGPL-2.1-or-later (Wine)
- **Usage**: the 32-bit (i386) translation provider and the matching Wine changes

## Wine modifications in this repository

`wine-11.12/` contains only the Wine 11.12 files this project changed (plus
`wine-11.12/FEX-FOR-MAC-CHANGES.md`, the list of changed files). They are licensed under the
LGPL-2.1-or-later like the rest of Wine. The complete corresponding source of a binary release is
Wine 11.12 from https://www.winehq.org/ with these files applied on top.

## 64-bit emulator (FEX ARM64EC) - experimental

- **Project**: https://github.com/FEX-Emu/FEX (the FEX-2607 snapshot)
- **License**: MIT (Copyright (c) 2019 Ryan Houdek and FEX-Emu contributors) - `licenses/FEX-MIT.txt`
- **Usage**: `libarm64ecfex.dll`, the ARM64EC build of FEX that runs 64-bit x86 Windows programs
  (bundled at `FEXforMac.app/Contents/Resources/fex64/`)
- **Our changes**: `patches/fex-arm64ec-*.patch` (MIT, same as FEX). These patches were written with AI
  assistance; the FEX-Emu project does not accept AI-generated code, so **do not send them to FEX-Emu**
  (see the "No Contributions to FEX-Emu" condition in LICENSE).
- **Statically linked into the DLL**: LLVM libc++, libc++abi, compiler-rt (Apache-2.0 with LLVM
  exceptions - `licenses/LLVM-Apache-2.0-with-LLVM-exception.txt`) and the mingw-w64 runtime
  (public domain / ZPL-2.1 / permissive; https://www.mingw-w64.org/).

## Wine changes for 64-bit support

`patches/wine-arm64ec-*.patch` change Wine's ARM64EC exception unwinding and syscall stubs
(LGPL-2.1-or-later like the rest of Wine - `licenses/Wine-LGPL-2.1.txt`). As for the earlier Wine changes,
the complete corresponding source is Wine 11.12 from https://www.winehq.org/ plus `wine-11.12/` and `patches/`.

## Not included

- **Steam** (Valve Corporation) is proprietary and is not distributed with FEX for Mac. Users download
  the installer from Valve themselves. "Steam" is a trademark of Valve; this project is not
  affiliated with or endorsed by Valve.
- **Games** and their data are never included.

---

## mkxp-z

- **Project**: https://github.com/mkxp-z/mkxp-z
- **License**: GNU General Public License v2.0 or later (GPL-2.0+); the bundled
  binary is linked with OpenSSL (Apache-2.0) for HTTPS downloads, so as
  distributed it is licensed under **GPL-3.0**
- **Usage**: Native ARM64 RPG Maker XP/VX/VX Ace engine (mkxp-z.aarch64)
- **Copyright**: Copyright (C) 2013-2021 Amaryllis Kulla and contributors
- **Note**: Our modifications to mkxp-z (CWD restore for ANGLE Metal
  compatibility, ibtool-free build, system iconv) are included in the
  `mkxp-z-patches/` directory and are also licensed under GPL-2.0+.
  Full modified source is available upon request per GPL terms.

### mkxp-z bundled dependencies (statically linked):

- **SDL2** — zlib License — https://www.libsdl.org/
- **ANGLE** — BSD 3-Clause — https://chromium.googlesource.com/angle/angle
- **OpenAL Soft** — LGPL-2.1 — https://openal-soft.org/
- **Ruby 3.1** — BSD 2-Clause — https://www.ruby-lang.org/
- **PhysFS** — zlib License — https://icculus.org/physfs/
- **OpenSSL 4** — Apache License 2.0 — https://www.openssl.org/ (dynamic libs
  `libssl.4.dylib`, `libcrypto.4.dylib` in Frameworks; lets games download files over HTTPS)
- **SDL2_image** — zlib License
- **SDL2_ttf** — zlib License
- **SDL_sound** — zlib License
- **libvorbis/libogg** — BSD 3-Clause — https://xiph.org/
- **pixman** — MIT — https://pixman.org/
- **uchardet** — MPL 1.1/GPL 2.0/LGPL 2.1 — https://www.freedesktop.org/wiki/Software/uchardet/
- **libtheora** — BSD 3-Clause — https://xiph.org/

---

## Full License Texts

License texts shipped in this repository (`licenses/`) and in the app (`Contents/Resources/licenses/`):
MIT (FEX), LGPL-2.1 (Wine), GPL-3.0 (mkxp-z as distributed), Apache-2.0 (OpenSSL, MoltenVK, Vulkan Loader),
Apache-2.0 with LLVM exceptions (libc++ etc.), and the PolyForm Noncommercial license of the original work
(`LICENSE`). The full text of every other license can be found at:

- **MIT**: https://opensource.org/licenses/MIT
- **LGPL-2.1**: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
- **GPL-2.0**: https://www.gnu.org/licenses/old-licenses/gpl-2.0.html
- **Apache-2.0**: https://www.apache.org/licenses/LICENSE-2.0
- **BSD 2-Clause**: https://opensource.org/licenses/BSD-2-Clause
- **BSD 3-Clause**: https://opensource.org/licenses/BSD-3-Clause
- **zlib**: https://opensource.org/licenses/Zlib
