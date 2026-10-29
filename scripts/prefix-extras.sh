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

# restart the server so the service starts on the next launch
"$WINE_BIN/wineserver" -k 2>/dev/null || true
echo "done"
