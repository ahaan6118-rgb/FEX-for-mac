# FEX for Mac — beta (0.7.0)

Run Windows programs and games on Apple silicon Macs **without Rosetta 2**.

> **Beta.** 32-bit (i386) Windows programs are supported. **64-bit x86 programs now work, but this is
> experimental and in an early stage**: simple programs and some real ones run, many others will not, and
> **Steam's current 64-bit client has not been tested yet** (see [Status](#status)). Expect rough edges.

Free to use, **not for sale** — see [License](#license).

## How it works

Everything that runs is native Apple-silicon (arm64) code. The x86 instructions of the Windows
program are translated to ARM64 on the fly by [FEX](https://github.com/FEX-Emu/FEX) inside
[Wine 11](https://www.winehq.org/), with a graphics path through Wine's Direct3D → OpenGL layer
and Apple's Metal (via ANGLE / MoltenVK). RPG Maker XP/VX/VX Ace games skip all of that and run on a
native [mkxp-z](https://github.com/mkxp-z/mkxp-z) engine.

## Status

Tested on an M1 Mac, macOS 26.

| Works (tested) | Notes |
|---|---|
| 32-bit Windows programs | Drag a `.exe` onto the app, or File → Open |
| A 32-bit DirectX 8 game (GameMaker 8 + FMOD) | Draws, plays, audio works, full 50 fps. Uses about 0.75 of a CPU core on an M1 (it is translating x86) |
| Steam (the last Windows-8-era 32-bit client) | Logs in, library loads. Valve has ended support for this client — see below |
| RPG Maker XP/VX/VX Ace games | Native `mkxp-z`; smoke-tested (starts, ANGLE-on-Metal renders) |

| Not working / limited | |
|---|---|
| **64-bit x86 programs (experimental, early)** | Run through FEX's ARM64EC build (bundled as `fex64/libarm64ecfex.dll`, set up automatically the first time you open a 64-bit `.exe`). Simple programs, a 64-bit RPG Maker game (it starts and draws) and the Pokémon Infinite Fusion launcher work. Sound, controls and speed in games are not fully checked, performance is untuned, and many programs will crash or hang. Programs that read Windows' shared system page (`0x7ffe0000`) directly will fail on Apple silicon |
| **Current Steam client** | **Not tested yet** on the 64-bit emulator. Valve's current client is 64-bit, so we still run the old 32-bit one; it shows a "Steam will stop running on Windows 8" banner and may stop working when Valve enforces that |
| Startup freezes | The embedded Chromium in Steam sometimes freezes at start (roughly 1 in 3 launches). The app restarts it automatically; this costs a minute or two |
| Exit crash dialog | Some games show a crash dialog when closing (a sound-library thread outliving its DLL) |
| Untested | Direct3D 11 games through DXMT, controllers, most of the Steam store |

## Using it

1. Open `FEXforMac.app` (see [First launch](#first-launch)).
2. Drag a Windows `.exe` onto the window, or use **Open…**. Recent programs are in **Recent Apps…**.
3. **Steam:** the first time, open Valve's `SteamSetup.exe` with the app (you download it from
   steampowered.com — it is not included). The app installs Steam and starts it with the right
   settings. Afterwards just click the **Steam** button. Use **Stop** (not force-quit) so Steam can
   save your login.

**Key remapping:** pick the game (Open… or a recent app), then **Game → Key Remapping…** (⌘K) and enter
pairs like `Space=Shift, Z=J` — pressing the key on the left acts as the key on the right, only in that game,
from its next start. RPG Maker games run natively instead: press F1 (fn+F1) in the game to change their keys.

When a program exits, the app also stops any of its Windows processes that are left running without a
Wine server (these used to keep spinning in the background).

Per-program settings live in `FEXforMac.app/Contents/Resources/profiles/*.profile` (plain text; the
runner `Resources/bin/fex-run` applies them). Logs: `~/Library/Logs/FEXforMac.log`.

### First launch

The beta is not notarized (no paid Apple developer account). If macOS blocks it: right-click the app →
Open, or run `xattr -dr com.apple.quarantine FEXforMac.app`.

## Building from source

The repository holds the app, runner, scripts and patches; Wine, FEX and the other large upstream
projects are not copied in — see `scripts/` and [CREDITS.md](CREDITS.md) for the exact upstream
versions and how the pieces are assembled.

```bash
# GUI app
swiftc -parse-as-library -o FEXforMac.app/Contents/MacOS/FEXforMac \
  src/gui/AppDelegate.swift -framework Cocoa -framework UniformTypeIdentifiers

# runner + profiles into an app bundle
scripts/install-runner.sh FEXforMac.app
```

- `scripts/build-fex.sh [wow64|arm64ec|all] [--install]` builds both FEX emulators (32-bit `xtajit.dll` and
  64-bit `libarm64ecfex.dll`) from one FEX tree.
- `scripts/assemble-vkmt-bundle.sh` assembles the Wine + FEX bundle.
- `scripts/make-launcher.sh "Name" 'C:\path\prog.exe'` builds a one-click launcher app for one program.
- mkxp-z: see [mkxp-z-patches/README.md](mkxp-z-patches/README.md).
- Wine changes: `wine-11.12/` holds the Wine 11.12 files we changed (`FEX-FOR-MAC-CHANGES.md`).
- FEX changes: `patches/`.

## License

**FEX for Mac's original code is free for non-commercial use under the
[PolyForm Noncommercial License 1.0.0](LICENSE). You may not sell it or use it to make money.**
It is "source-available", not OSI open source.

The license covers only the project's original work. Wine (LGPL-2.1+), FEX (MIT), mkxp-z (GPL-2.0+, GPL-3.0 as distributed),
and the other bundled components keep their own licenses and rights — see
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) and the license texts in [licenses/](licenses/).
Parts of this project were written with AI assistance; **please do not send this code to the FEX-Emu
project** (a condition of the license). Version 0.3.0 was published under MIT
([LICENSE-MIT-v0.3.0.txt](LICENSE-MIT-v0.3.0.txt)) and remains available under MIT.

Not affiliated with or endorsed by Valve, CodeWeavers, Apple or the Wine project. Steam is a
trademark of Valve and is not included.
