#!/bin/bash
# build-fex.sh - build both FEX emulator DLLs from the one FEX tree (willfaust-FEX, branch fex-mac-unified).
#
#   scripts/build-fex.sh [wow64|arm64ec|all] [--install]
#
#   wow64   -> build/fex-wow64/Bin/libwow64fex.dll     32-bit programs (installed as Wine's xtajit.dll)
#   arm64ec -> build/fex-arm64ec/Bin/libarm64ecfex.dll 64-bit programs (installed as Resources/fex64/libarm64ecfex.dll)
#
# --install copies the results into FEXforMac.app (and the default prefix's system32 for the 64-bit one).
# Environment: FEX_LTO=ON|OFF (default OFF: with these toolchains LTO breaks both links,
#   wow64 on compiler-rt -> FlushInstructionCache, arm64ec on EC symbols), FEX_TUNE=<-mcpu> (default apple-m1), FEX_JOBS (default: all cores).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/willfaust-FEX"
# Wine keeps the TEB in x28 on macOS (Darwin scrubs x18), so every function in these DLLs must leave x28 alone.
# arm64ec: llvm-mingw 20261020 with its winnt.h patched so NtCurrentTeb() reads x28 (patches/llvm-mingw-teb-x28.patch).
#   ARM64EC code never allocates x28, so the toolchain's prebuilt C++ runtimes are safe.
# wow64 (aarch64): llvm-mingw 20250514, whose aarch64 libc++/libc++abi/libunwind were rebuilt with -ffixed-x28
#   (build-runtimes-x28; originals kept as *.orig). The stock runtimes use x28 as a scratch register and corrupt the
#   TEB when they call back into FEX (operator new -> rpmalloc -> TlsGetValue). Its remaining x18 TEB/TLS reads are
#   rewritten to x28 after linking by wine-11.12/tools/vkmt-fix-x18-all.py.
TC="$ROOT/llvm-mingw-new-dl/llvm-mingw-20261020-ucrt-macos-universal/bin"
TC32="$ROOT/llvm-mingw-20250514-ucrt-macos-universal/bin"
grep -q '__asm__("x28")' "$TC/../aarch64-w64-mingw32/include/winnt.h" \
    || { echo "llvm-mingw winnt.h is not patched (apply patches/llvm-mingw-teb-x28.patch)" >&2; exit 1; }
[ -f "$TC32/../aarch64-w64-mingw32/lib/libc++.a.orig" ] \
    || { echo "$TC32 still has the stock aarch64 C++ runtimes (install build-runtimes-x28 first)" >&2; exit 1; }
APP="$ROOT/FEXforMac.app/Contents/Resources"
LTO="${FEX_LTO:-OFF}"
TUNE="${FEX_TUNE:-apple-m1}"
JOBS="${FEX_JOBS:-$(sysctl -n hw.ncpu)}"

WHAT="all"; INSTALL=0
for a in "$@"; do
    case "$a" in
        wow64|arm64ec|all) WHAT="$a" ;;
        --install) INSTALL=1 ;;
        *) echo "usage: $0 [wow64|arm64ec|all] [--install]" >&2; exit 2 ;;
    esac
done

# x28 is the TEB in Wine's PE code on macOS (Darwin scrubs x18), so the compiler must never allocate it
COMMON=(-G Ninja -DCMAKE_MAKE_PROGRAM=/opt/homebrew/bin/ninja -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_TOOLCHAIN_FILE="$SRC/Data/CMake/toolchain_mingw.cmake"
        -DCMAKE_C_FLAGS=-ffixed-x28 -DCMAKE_CXX_FLAGS=-ffixed-x28
        -DENABLE_ASSERTIONS=OFF -DENABLE_JEMALLOC=OFF -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF
        -DENABLE_OFFLINE_TELEMETRY=OFF -DENABLE_CCACHE=OFF -DBUILD_TESTING=OFF -DBUILD_FEXCONFIG=OFF -DBUILD_THUNKS=OFF
)

build() { # <name> <toolchain bin> <target> <extra cmake args...>
    local name="$1" tc="$2" target="$3"; shift 3
    local dir="$ROOT/build/fex-$name"
    mkdir -p "$dir"
    if [ ! -f "$dir/build.ninja" ] || [ "$(cat "$dir/.lto" 2>/dev/null)" != "$LTO $TUNE" ]; then
        PATH="$tc:$PATH" cmake -S "$SRC" -B "$dir" "${COMMON[@]}" -DENABLE_LTO="$LTO" -DTUNE_CPU="$TUNE" "$@" >"$dir/configure.log" 2>&1 \
            || { tail -30 "$dir/configure.log"; exit 1; }
        echo "$LTO $TUNE" > "$dir/.lto"
    fi
    PATH="$tc:$PATH" /opt/homebrew/bin/ninja -C "$dir" -j "$JOBS" "$target"
}

if [ "$WHAT" = wow64 ] || [ "$WHAT" = all ]; then
    build wow64 "$TC32" wow64fex -DMINGW_TRIPLE=aarch64-w64-mingw32 -DENABLE_GUEST_WINDOW=ON
    D="$ROOT/build/fex-wow64/Bin/libwow64fex.dll"
    if [ "$D" -nt "$D.x18fixed" ]; then
        LLVM_MINGW_BIN="$TC32" python3 "$ROOT/wine-11.12/tools/vkmt-fix-x18-all.py" "$D" >/dev/null
        touch "$D.x18fixed"
    fi
fi
if [ "$WHAT" = arm64ec ] || [ "$WHAT" = all ]; then
    # No -mcpu for arm64ec: clang 23 crashes with "Failed to evaluate function length in SEH unwind info"
    TUNE=none build arm64ec "$TC" arm64ecfex -DMINGW_TRIPLE=arm64ec-w64-mingw32 -DENABLE_GUEST_WINDOW=OFF
    # Compiler-generated TLS (thread_local, libc++abi's __cxa_get_globals) still reads [x18, #0x58]
    D="$ROOT/build/fex-arm64ec/Bin/libarm64ecfex.dll"
    if [ "$D" -nt "$D.x18fixed" ]; then
        LLVM_MINGW_BIN="$TC" python3 "$ROOT/wine-11.12/tools/vkmt-fix-x18-tls.py" "$D" >/dev/null
        touch "$D.x18fixed"
    fi
fi

if [ $INSTALL = 1 ]; then
    if [ "$WHAT" = wow64 ] || [ "$WHAT" = all ]; then
        install -m 755 "$ROOT/build/fex-wow64/Bin/libwow64fex.dll" "$APP/wine/lib/wine/aarch64-windows/xtajit.dll"
        echo "installed xtajit.dll"
    fi
    if [ "$WHAT" = arm64ec ] || [ "$WHAT" = all ]; then
        install -m 755 "$ROOT/build/fex-arm64ec/Bin/libarm64ecfex.dll" "$APP/fex64/libarm64ecfex.dll"
        echo "installed fex64/libarm64ecfex.dll (the app copies it into a prefix on the next 64-bit run)"
    fi
fi
