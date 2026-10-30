# FEX for Mac — beta (0.5.0)

Run Windows programs and games on Apple silicon Macs **without Rosetta 2**.

> **Beta, 32-bit only.** 32-bit (i386) Windows programs are supported. **64-bit x86 programs are not
> supported yet** (see [Status](#status)). Expect rough edges.

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
| A 32-bit DirectX 8 game (GameMaker 8 + FMOD) | Draws, plays, audio works. Higher CPU than a native PC (it is translating x86) |
| Steam (the last Windows-8-era 32-bit client) | Logs in, library loads. Valve has ended support for this client — see below |
| RPG Maker XP/VX/VX Ace games | Native `mkxp-z`; smoke-tested (starts, ANGLE-on-Metal renders) |

| Not working / limited | |
|---|---|
| **64-bit x86 programs** | The bundled 64-bit emulator is an unfinished interpreter, not FEX. Most 64-bit programs fail. Real 64-bit support (FEX ARM64EC) is the next big project |
| **Current Steam client** | Valve's current client needs 64-bit emulation. We run the old 32-bit one; it shows a "Steam will stop running on Windows 8" banner and may stop working when Valve enforces that |
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

- `scripts/assemble-vkmt-bundle.sh` assembles the Wine + FEX bundle.
- `scripts/make-launcher.sh "Name" 'C:\path\prog.exe'` builds a one-click launcher app for one program.
- mkxp-z: see [mkxp-z-patches/README.md](mkxp-z-patches/README.md).
- Wine changes: `wine-11.12/` holds the Wine 11.12 files we changed (`FEX-FOR-MAC-CHANGES.md`).
- FEX changes: `patches/`.

## License

**FEX for Mac's original code is free for non-commercial use under the
[PolyForm Noncommercial License 1.0.0](LICENSE). You may not sell it or use it to make money.**
It is "source-available", not OSI open source.

The license covers only the project's original work. Wine (LGPL-2.1+), FEX (MIT), mkxp-z (GPL-2.0+),
and the other bundled components keep their own licenses and rights — see
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). Version 0.3.0 was published under MIT
([LICENSE-MIT-v0.3.0.txt](LICENSE-MIT-v0.3.0.txt)) and remains available under MIT.

Not affiliated with or endorsed by Valve, CodeWeavers, Apple or the Wine project. Steam is a
trademark of Valve and is not included.
