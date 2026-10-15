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

## mkxp-z

- **Project**: https://github.com/mkxp-z/mkxp-z
- **License**: GNU General Public License v2.0 or later (GPL-2.0+)
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
- **SDL2_image** — zlib License
- **SDL2_ttf** — zlib License
- **SDL_sound** — zlib License
- **libvorbis/libogg** — BSD 3-Clause — https://xiph.org/
- **pixman** — MIT — https://pixman.org/
- **uchardet** — MPL 1.1/GPL 2.0/LGPL 2.1 — https://www.freedesktop.org/wiki/Software/uchardet/
- **libtheora** — BSD 3-Clause — https://xiph.org/

---

## Full License Texts

The full text of each license can be found at:

- **MIT**: https://opensource.org/licenses/MIT
- **LGPL-2.1**: https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
- **GPL-2.0**: https://www.gnu.org/licenses/old-licenses/gpl-2.0.html
- **Apache-2.0**: https://www.apache.org/licenses/LICENSE-2.0
- **BSD 2-Clause**: https://opensource.org/licenses/BSD-2-Clause
- **BSD 3-Clause**: https://opensource.org/licenses/BSD-3-Clause
- **zlib**: https://opensource.org/licenses/Zlib
