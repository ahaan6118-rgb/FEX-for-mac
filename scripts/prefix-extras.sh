#!/bin/bash
# Prefix fixes the Steam client needs on top of a plain wineboot --init prefix.
# Idempotent; safe to run on an existing prefix.
#
#   scripts/prefix-extras.sh [WINEPREFIX]     (default: ~/.fex-for-mac)
#
#  * rsaenh      - registers the RSA crypto provider (CryptAcquireContext), needed for TLS to Valve
#  * oleacc.dll  - Chromium's libcef imports it via UIAutomationCore
#  * nsiproxy    - registers the NSI proxy driver service. Without it GetExtendedTcpTable() fails,
#                  and Steam rejects the web UI's websocket as an "unknown source" (owner-PID check).
#  * TEMP/TMP    - environment variables a hand-made prefix lacks
#  * vulkan-1    - Steam's Chromium ships its own Vulkan loader and SwiftShader ICD next to
#                  steamwebhelper.exe; Wine must prefer that loader over its builtin one there.
#  * FEX_HALFBARRIERTSOENABLED=0 - FEX's half-barrier TSO makes unaligned accesses non-atomic;
#                  Chromium (steamwebhelper) then freezes at startup in most launches. FEX reads
#                  FEX_* options from the process environment, so set it in HKCU\Environment.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="${FEX_APP:-$ROOT/FEXforMac.app}"
WINE_BIN="$APP/Contents/Resources/wine/bin"
WINE_LIB="$APP/Contents/Resources/wine/lib/wine"
export WINEPREFIX="${1:-$HOME/.fex-for-mac}"
export WINEDEBUG=-all

wine() { perl -e 'alarm 90; exec @ARGV' "$WINE_BIN/wine" "$@"; }

echo "prefix: $WINEPREFIX"

# oleacc.dll: aarch64 into system32, i386 into syswow64
[ -f "$WINE_LIB/aarch64-windows/oleacc.dll" ] && cp "$WINE_LIB/aarch64-windows/oleacc.dll" "$WINEPREFIX/drive_c/windows/system32/oleacc.dll"
[ -f "$WINE_LIB/i386-windows/oleacc.dll" ] && cp "$WINE_LIB/i386-windows/oleacc.dll" "$WINEPREFIX/drive_c/windows/syswow64/oleacc.dll"

# rsaenh provider, both bitnesses
wine regsvr32 /s rsaenh.dll >/dev/null 2>&1 || true
wine C:\\windows\\syswow64\\regsvr32.exe /s rsaenh.dll >/dev/null 2>&1 || true

# 32-bit COM registrations a hand-made prefix lacks (e.g. WebBrowser control: Delphi/VCL programs create it)
for d in ieframe shdocvw mshtml; do
    wine C:\\windows\\syswow64\\regsvr32.exe /s $d.dll >/dev/null 2>&1 || true
done

# 32-bit audio/input COM registrations (DirectSound, MMDevice, DirectInput); audio itself works through
# winecoreaudio.drv now that its WoW64 thunks translate guest pointers
for d in mmdevapi dsound dinput dinput8 devenum; do
    wine C:\\windows\\syswow64\\regsvr32.exe /s $d.dll >/dev/null 2>&1 || true
done

# nsiproxy driver service (same values wine.inf uses)
K='HKLM\System\CurrentControlSet\Services\nsiproxy'
add() { wine reg add "$K" "$@" /f >/dev/null 2>&1; }
add /v Type /t REG_DWORD /d 1
add /v Start /t REG_DWORD /d 2
add /v ErrorControl /t REG_DWORD /d 1
add /v Tag /t REG_DWORD /d 1
add /v ImagePath /t REG_EXPAND_SZ /d 'C:\windows\system32\drivers\nsiproxy.sys'
add /v Group /t REG_SZ /d "System Bus Extender"
add /v DisplayName /t REG_SZ /d "NSI Proxy"
add /v Description /t REG_SZ /d "NSI proxy service"

# Chromium's own Vulkan loader + SwiftShader software renderer (used when the GPU is disabled)
wine reg add 'HKCU\Software\Wine\DllOverrides' /v vulkan-1 /t REG_SZ /d native,builtin /f >/dev/null 2>&1
CEF="$WINEPREFIX/drive_c/Program Files/Steam/bin/cef/cef.win7"
if [ -f "$CEF/vk_swiftshader.dll" ]; then
    printf '{"file_format_version":"1.0.0","ICD":{"library_path":".\\\\vk_swiftshader.dll","api_version":"1.3.0"}}' > "$CEF/vk_swiftshader_icd.json"
fi

# FEX option, see above
wine reg add 'HKCU\Environment' /v FEX_HALFBARRIERTSOENABLED /t REG_SZ /d 0 /f >/dev/null 2>&1

# TEMP/TMP: a hand-made prefix has none, so GetTempPath() falls back to C:\windows\ and programs fail to create
# their temp dirs (GameMaker games: "Failed to create tmp dir")
WINUSER="$(wine cmd /c 'echo %USERNAME%' 2>/dev/null | tr -d '\r\n ')"
if [ -n "$WINUSER" ]; then
    mkdir -p "$WINEPREFIX/drive_c/users/$WINUSER/AppData/Local/Temp"
    for v in TEMP TMP; do
        wine reg add 'HKCU\Environment' /v $v /t REG_SZ /d "C:\\users\\$WINUSER\\AppData\\Local\\Temp" /f >/dev/null 2>&1
    done
fi

# FEX x87: full 80-bit precision (reduced precision corrupts 64-bit FILD/FISTP copies used by Delphi programs)
wine reg add 'HKCU\Environment' /v FEX_X87REDUCEDPRECISION /t REG_SZ /d 0 /f >/dev/null 2>&1

# restart the server so the service starts on the next launch
"$WINE_BIN/wineserver" -k 2>/dev/null || true
echo "done"
