#!/bin/bash
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WINE_BUILD="${ROOT}/wine-11.12/build-full"
FEX_BUILD="${ROOT}/build-fex-guestwindow"
APP="${ROOT}/FEXforMac.app"
WINE_DIR="${APP}/Contents/Resources/wine"
LLVM_BIN="${ROOT}/llvm-mingw-20250514-ucrt-macos-universal/bin"

echo "=== Assembling FEX for Mac bundle ==="

[ -f "${WINE_BUILD}/tools/wine/wine" ] || { echo "ERROR: Wine not built"; exit 1; }
[ -f "${WINE_BUILD}/server/wineserver" ] || { echo "ERROR: wineserver not built"; exit 1; }

rm -rf "${WINE_DIR}"
mkdir -p "${WINE_DIR}/bin"
mkdir -p "${WINE_DIR}/lib/wine/aarch64-unix"
mkdir -p "${WINE_DIR}/lib/wine/aarch64-windows"
mkdir -p "${WINE_DIR}/lib/wine/x86_64-windows"
mkdir -p "${WINE_DIR}/lib/wine/i386-windows"
mkdir -p "${WINE_DIR}/share/wine/fonts"

echo "Copying wine loader and wineserver..."
cp "${WINE_BUILD}/tools/wine/wine" "${WINE_DIR}/bin/wine"
cp "${WINE_BUILD}/loader/wine" "${WINE_DIR}/lib/wine/aarch64-unix/wine"
cp "${WINE_BUILD}/server/wineserver" "${WINE_DIR}/bin/wineserver"

echo "Copying Unix .so files..."
find "${WINE_BUILD}/dlls" -maxdepth 2 -name "*.so" -type f | while read f; do
    base=$(basename "$f")
    cp "$f" "${WINE_DIR}/lib/wine/aarch64-unix/${base}"
done

for arch in aarch64-windows x86_64-windows i386-windows; do
    echo "Copying ${arch} PE files..."
    find "${WINE_BUILD}/dlls" "${WINE_BUILD}/programs" -path "*/${arch}/*" -type f \
        \( -name "*.dll" -o -name "*.exe" -o -name "*.drv" -o -name "*.sys" \
           -o -name "*.cpl" -o -name "*.ocx" -o -name "*.acm" -o -name "*.ax" \
           -o -name "*.tlb" -o -name "*.msstyles" -o -name "*.com" \) 2>/dev/null | while read f; do
        cp "$f" "${WINE_DIR}/lib/wine/${arch}/$(basename "$f")"
    done
done

if [ -f "${WINE_DIR}/lib/wine/aarch64-windows/xtajit64.dll" ]; then
    echo "xtajit64.dll: OK (from Wine build)"
else
    echo "WARNING: xtajit64.dll not found"
fi

if [ -f "${FEX_BUILD}/Bin/libwow64fex.dll" ]; then
    # The 32-bit provider must be the guest-window build (willfaust FEX fork, with the JIT state
    # register moved off x28) and must never use x28, which Wine keeps the TEB in.
    CACHE="${FEX_BUILD}/CMakeCache.txt"
    grep -q "ENABLE_GUEST_WINDOW:BOOL=ON" "${CACHE}" \
        || { echo "ERROR: ${FEX_BUILD} was not configured with ENABLE_GUEST_WINDOW=ON"; exit 1; }
    grep -q "CMAKE_CXX_FLAGS:STRING=.*-ffixed-x28" "${CACHE}" \
        || { echo "ERROR: ${FEX_BUILD} was not built with -ffixed-x28 (reconfigure with CMAKE_CXX_FLAGS and CMAKE_C_FLAGS set to -ffixed-x28)"; exit 1; }

    echo "Installing xtajit.dll (FEX wow64 provider, guest-window build)..."
    cp "${FEX_BUILD}/Bin/libwow64fex.dll" "${WINE_DIR}/lib/wine/aarch64-windows/xtajit.dll"
    # xtajit.dll is not linked by winegcc, so its x18 TEB reads are not rewritten automatically.
    python3 "${ROOT}/wine-11.12/tools/vkmt-fix-x18-all.py" "${WINE_DIR}/lib/wine/aarch64-windows/xtajit.dll" \
        || { echo "ERROR: x18->x28 patch of xtajit.dll failed"; exit 1; }
else
    echo "WARNING: FEX wow64 provider not built"
fi

echo "Running x18->x28 TEB fix on ARM64 PE binaries..."
export LLVM_MINGW_BIN="${LLVM_BIN}"
find "${WINE_DIR}/lib/wine/aarch64-windows" \( -name "*.dll" -o -name "*.exe" \) ! -name xtajit.dll 2>/dev/null | while read f; do
    python3 "${ROOT}/wine-11.12/tools/vkmt-fix-x18-tls.py" "$f" 2>/dev/null || true
done

echo "Copying fonts..."
find "${ROOT}/wine-11.12/fonts" \( -name "*.ttf" -o -name "*.fon" \) 2>/dev/null | while read f; do
    cp "$f" "${WINE_DIR}/share/wine/fonts/"
done

echo "Copying NLS files..."
mkdir -p "${WINE_DIR}/share/wine/nls"
cp "${ROOT}/wine-11.12/nls/"*.nls "${WINE_DIR}/share/wine/nls/"

echo "Copying wine.inf..."
cp "${WINE_BUILD}/loader/wine.inf" "${WINE_DIR}/share/wine/wine.inf"

# win32u.so loads libfreetype.6.dylib with dlopen(). DYLD_* variables do not reach the Wine
# processes, so give it an rpath to the bundled Frameworks directory instead.
if [ -f "${APP}/Contents/Frameworks/libfreetype.6.dylib" ]; then
    WIN32U="${WINE_DIR}/lib/wine/aarch64-unix/win32u.so"
    RPATH="@loader_path/../../../../../Frameworks"
    otool -l "${WIN32U}" | grep -q "path ${RPATH} " || install_name_tool -add_rpath "${RPATH}" "${WIN32U}"
else
    echo "WARNING: Contents/Frameworks/libfreetype.6.dylib missing; Wine will have no TrueType fonts"
fi

echo "Code signing..."
codesign -s - -f "${WINE_DIR}/bin/wine" 2>/dev/null || true
codesign -s - -f "${WINE_DIR}/bin/wineserver" 2>/dev/null || true
codesign -s - -f "${WINE_DIR}/lib/wine/aarch64-unix/wine" 2>/dev/null || true
codesign -s - -f "${WINE_DIR}/lib/wine/aarch64-unix/win32u.so" 2>/dev/null || true
codesign -s - -f "${WINE_DIR}/lib/wine/aarch64-windows/xtajit.dll" 2>/dev/null || true

# Optional: link the bundle's PEs into an existing prefix the same way the app does at launch
# (system32 <- aarch64/x86_64, syswow64 <- i386). The 32-bit loader needs syswow64 populated.
PREFIX="${FEXMAC_PREFIX:-$HOME/.fex-for-mac}"
if [ -d "${PREFIX}/drive_c/windows" ]; then
    echo "Linking PEs into prefix ${PREFIX}..."
    mkdir -p "${PREFIX}/drive_c/windows/system32" "${PREFIX}/drive_c/windows/syswow64"
    link_pes() {
        for f in "$1"/*.dll "$1"/*.exe "$1"/*.drv "$1"/*.sys "$1"/*.ocx "$1"/*.cpl "$1"/*.acm "$1"/*.ax; do
            [ -e "$f" ] || continue
            dest="$2/$(basename "$f")"
            # replace stale copies and dangling links, keep up-to-date links
            [ -L "$dest" ] && [ "$(readlink "$dest")" = "$f" ] && continue
            rm -f "$dest"
            ln -s "$f" "$dest"
        done
    }
    link_pes "${WINE_DIR}/lib/wine/aarch64-windows" "${PREFIX}/drive_c/windows/system32"
    link_pes "${WINE_DIR}/lib/wine/i386-windows" "${PREFIX}/drive_c/windows/syswow64"
fi

echo ""
echo "=== Bundle assembly complete ==="
echo "Unix .so files: $(ls ${WINE_DIR}/lib/wine/aarch64-unix/*.so 2>/dev/null | wc -l | tr -d ' ')"
echo "aarch64 PEs:    $(ls ${WINE_DIR}/lib/wine/aarch64-windows/*.{dll,exe} 2>/dev/null | wc -l | tr -d ' ')"
echo "x86_64 PEs:     $(ls ${WINE_DIR}/lib/wine/x86_64-windows/*.{dll,exe} 2>/dev/null | wc -l | tr -d ' ')"
echo "i386 PEs:       $(ls ${WINE_DIR}/lib/wine/i386-windows/*.{dll,exe} 2>/dev/null | wc -l | tr -d ' ')"
echo "Fonts:          $(ls ${WINE_DIR}/share/wine/fonts/* 2>/dev/null | wc -l | tr -d ' ')"

# generic program runner (profiles, startup-freeze restart) and prefix fixes
"${ROOT}/scripts/install-runner.sh" "${APP}"
