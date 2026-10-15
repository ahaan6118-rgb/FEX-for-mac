# FEX for Mac

Run x86_64 Windows games natively on Apple Silicon — no Rosetta 2 required.

## What it does

FEX for Mac translates x86_64 Windows PE binaries to ARM64 on macOS using:

- **FEXCore** — JIT translates x86_64 instructions to ARM64
- **Wine 11** — Windows API compatibility layer
- **DXMT** — D3D11 → Metal GPU translation
- **mkxp-z** — Native ARM64 RPG Maker engine (no Wine needed for RPG Maker games)

## Features

- Drag-and-drop any `.exe` to run it
- **RPG Maker auto-detection**: Games using RGSS (RPG Maker XP/VX/VX Ace) run natively through mkxp-z with ANGLE Metal rendering — no JIT translation overhead
- **Pokemon Infinite Fusion support**: Auto-installs and runs natively on ARM64
- All other x86_64 Windows games go through the FEXCore + Wine pipeline

## How it works

When you open a `.exe`:

1. FEX for Mac checks if it's an RPG Maker game (looks for `Game.ini` with an RGSS library)
2. **If RPG Maker** → launches natively via mkxp-z (ARM64 binary, ANGLE Metal for OpenGL, OpenAL for audio)
3. **If not** → runs through FEXCore JIT + Wine + DXMT Metal

## Building from source

### Prerequisites

- macOS 14+ on Apple Silicon
- Xcode Command Line Tools (`xcode-select --install`)
- CMake 3.14+
- Meson + Ninja (for mkxp-z: `brew install meson ninja`)

### Build the GUI app

```bash
swiftc -parse-as-library -o RosettaKiller.app/Contents/MacOS/RosettaKiller \
  src/gui/AppDelegate.swift -framework Cocoa -framework UniformTypeIdentifiers
```

### Build mkxp-z (RPG Maker engine)

See [mkxp-z-patches/README.md](mkxp-z-patches/README.md) for build instructions.

### Build FEXCore bridge

```bash
cmake -B build_macos -DCMAKE_BUILD_TYPE=Release
cmake --build build_macos
```

## License

The FEX for Mac launcher (Swift GUI) is under the [MIT License](LICENSE).

This project bundles components under their own licenses — see
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for details.
The mkxp-z modifications are under GPL-2.0+ (same as upstream mkxp-z).
