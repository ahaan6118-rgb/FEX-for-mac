#!/bin/bash
# Setup a Wine prefix for FEX-for-Mac
# Pre-populates system32 with built DLLs, then runs wineboot
# Usage: setup_prefix.sh <wine_dir> [prefix_dir]
#   wine_dir: path to Wine build (contains dlls/, programs/, loader/)
#   prefix_dir: WINEPREFIX (default: ~/.fex-for-mac)

set -e

WINE_DIR="${1:?Usage: setup_prefix.sh <wine_dir> [prefix_dir]}"
PREFIX="${2:-$HOME/.fex-for-mac}"
BRIDGE_PATH="${FEXBRIDGE_PATH:-}"

if [ ! -f "$WINE_DIR/loader/wine" ]; then
    echo "Error: $WINE_DIR/loader/wine not found" >&2
    exit 1
fi

SYS32="$PREFIX/drive_c/windows/system32"
SYSWOW="$PREFIX/drive_c/windows/syswow64"

mkdir -p "$SYS32" "$SYSWOW" "$PREFIX/drive_c/users" "$PREFIX/dosdevices"

# Create dosdevices symlinks
[ -L "$PREFIX/dosdevices/c:" ] || ln -s "../drive_c" "$PREFIX/dosdevices/c:"
[ -L "$PREFIX/dosdevices/z:" ] || ln -s "/" "$PREFIX/dosdevices/z:"

# Copy x86_64 PE DLLs to system32
DLL_SRC="$WINE_DIR/dlls"
echo "Populating system32 from $DLL_SRC..."
count=0
for dll in "$DLL_SRC"/*/x86_64-windows/*.dll "$DLL_SRC"/*/x86_64-windows/*.drv "$DLL_SRC"/*/x86_64-windows/*.ocx "$DLL_SRC"/*/x86_64-windows/*.sys "$DLL_SRC"/*/x86_64-windows/*.cpl "$DLL_SRC"/*/x86_64-windows/*.acm "$DLL_SRC"/*/x86_64-windows/*.ax; do
    [ -f "$dll" ] || continue
    name=$(basename "$dll")
    if [ ! -f "$SYS32/$name" ] || [ "$dll" -nt "$SYS32/$name" ]; then
        cp "$dll" "$SYS32/$name"
        count=$((count + 1))
    fi
done
echo "  Copied/updated $count DLLs"

# Copy PE executables (wineboot.exe, services.exe, etc.)
EXE_SRC="$WINE_DIR/programs"
exe_count=0
for exe in "$EXE_SRC"/*/x86_64-windows/*.exe; do
    [ -f "$exe" ] || continue
    name=$(basename "$exe")
    if [ ! -f "$SYS32/$name" ] || [ "$exe" -nt "$SYS32/$name" ]; then
        cp "$exe" "$SYS32/$name"
        exe_count=$((exe_count + 1))
    fi
done
echo "  Copied/updated $exe_count EXEs"

# Create minimal registry if not present
if [ ! -f "$PREFIX/system.reg" ]; then
    cat > "$PREFIX/system.reg" << 'REGEOF'
WINE REGISTRY Version 2
;; All keys relative to \\Machine
#arch=win64

[System\\CurrentControlSet\\Control\\Session Manager\\Environment] 1234567890
"PATH"="C:\\windows\\system32;C:\\windows"
"TEMP"="C:\\users\\Public\\Temp"
"TMP"="C:\\users\\Public\\Temp"
"windir"="C:\\windows"
"SystemRoot"="C:\\windows"
"ProgramFiles"="C:\\Program Files"
REGEOF
fi

if [ ! -f "$PREFIX/user.reg" ]; then
    cat > "$PREFIX/user.reg" << 'REGEOF'
WINE REGISTRY Version 2
;; All keys relative to \\User\\Current
REGEOF
fi

if [ ! -f "$PREFIX/userdef.reg" ]; then
    cat > "$PREFIX/userdef.reg" << 'REGEOF'
WINE REGISTRY Version 2
;; All keys relative to \\User\\.Default
REGEOF
fi

# Create user directories
USERNAME=$(whoami)
USER_DIR="$PREFIX/drive_c/users/$USERNAME"
mkdir -p "$USER_DIR/AppData/Local" "$USER_DIR/AppData/Roaming" "$USER_DIR/AppData/LocalLow"
mkdir -p "$USER_DIR/Documents" "$USER_DIR/Desktop" "$USER_DIR/Downloads"
mkdir -p "$PREFIX/drive_c/users/Public/Temp"
mkdir -p "$PREFIX/drive_c/ProgramData"
mkdir -p "$PREFIX/drive_c/Program Files"
mkdir -p "$PREFIX/drive_c/Program Files (x86)"
mkdir -p "$PREFIX/drive_c/windows/temp"

# Run wineboot if bridge is available
if [ -n "$BRIDGE_PATH" ] && [ -f "$BRIDGE_PATH" ]; then
    echo "Running wineboot --init..."
    FEXBRIDGE_PATH="$BRIDGE_PATH" \
    WINEPREFIX="$PREFIX" \
    WINEDEBUG="-all" \
    "$WINE_DIR/wine" wineboot --init 2>/dev/null || true
    # Fix arch tag — wineboot under x86_64 JIT may write win32
    for reg in "$PREFIX/system.reg" "$PREFIX/user.reg" "$PREFIX/userdef.reg"; do
        [ -f "$reg" ] && sed -i '' 's/^#arch=win32$/#arch=win64/' "$reg"
    done
    echo "  wineboot completed"
fi

echo "Wine prefix ready at $PREFIX"
echo "  DLLs: $(ls "$SYS32"/*.dll 2>/dev/null | wc -l | tr -d ' ')"
echo "  EXEs: $(ls "$SYS32"/*.exe 2>/dev/null | wc -l | tr -d ' ')"
