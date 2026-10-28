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
    echo "Installing xtajit.dll (FEX wow64 provider)..."
    cp "${FEX_BUILD}/Bin/libwow64fex.dll" "${WINE_DIR}/lib/wine/aarch64-windows/xtajit.dll"
else
    echo "WARNING: FEX wow64 provider not built"
fi

echo "Running x18->x28 TEB fix on ARM64 PE binaries..."
export LLVM_MINGW_BIN="${LLVM_BIN}"
find "${WINE_DIR}/lib/wine/aarch64-windows" \( -name "*.dll" -o -name "*.exe" \) 2>/dev/null | while read f; do
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

echo "Code signing..."
codesign -s - -f "${WINE_DIR}/bin/wine" 2>/dev/null || true
codesign -s - -f "${WINE_DIR}/bin/wineserver" 2>/dev/null || true
codesign -s - -f "${WINE_DIR}/lib/wine/aarch64-unix/wine" 2>/dev/null || true

echo ""
echo "=== Bundle assembly complete ==="
echo "Unix .so files: $(ls ${WINE_DIR}/lib/wine/aarch64-unix/*.so 2>/dev/null | wc -l | tr -d ' ')"
echo "aarch64 PEs:    $(ls ${WINE_DIR}/lib/wine/aarch64-windows/*.{dll,exe} 2>/dev/null | wc -l | tr -d ' ')"
echo "x86_64 PEs:     $(ls ${WINE_DIR}/lib/wine/x86_64-windows/*.{dll,exe} 2>/dev/null | wc -l | tr -d ' ')"
echo "i386 PEs:       $(ls ${WINE_DIR}/lib/wine/i386-windows/*.{dll,exe} 2>/dev/null | wc -l | tr -d ' ')"
echo "Fonts:          $(ls ${WINE_DIR}/share/wine/fonts/* 2>/dev/null | wc -l | tr -d ' ')"
