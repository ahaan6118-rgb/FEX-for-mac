#!/bin/bash
# Builds "Steam (FEX).app": a double-clickable launcher for the Steam client in the FEX for Mac Wine prefix.
#
#   scripts/make-steam-launcher.sh [output-dir]      (default: repo root)
#
# The launcher points at FEXforMac.app (the Wine + FEX engine) by absolute path, so rebuild it if you move
# the repository. It never touches the native macOS Steam. Ad-hoc signed, no Apple Developer account needed.
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT}"
ENGINE="${FEX_APP:-$ROOT/FEXforMac.app}"
APP="$OUT/Steam (FEX).app"

[ -x "$ENGINE/Contents/Resources/wine/bin/wine" ] || { echo "ERROR: no Wine in $ENGINE" >&2; exit 1; }

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Steam (FEX)</string>
    <key>CFBundleDisplayName</key><string>Steam (FEX)</string>
    <key>CFBundleIdentifier</key><string>com.fexformac.steam</string>
    <key>CFBundleVersion</key><string>1.0</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundleExecutable</key><string>SteamFEX</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>LSMinimumSystemVersion</key><string>14.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

cat > "$APP/Contents/MacOS/SteamFEX" <<LAUNCHER
#!/bin/bash
# Launches the Windows Steam client through Wine + FEX. Stays alive while Steam runs so the Dock shows it;
# quitting this app from the Dock stops Steam and the Wine server for this prefix.
ENGINE="\${FEX_APP:-$ENGINE}"
export WINEPREFIX="\${WINEPREFIX:-\$HOME/.fex-for-mac}"
export WINEDEBUG=-all
# FEX's half-barrier TSO makes Chromium freeze at startup (also set in the prefix by scripts/prefix-extras.sh)
export FEX_HALFBARRIERTSOENABLED=0
LOG="\$HOME/Library/Logs/SteamFEX.log"
WINE="\$ENGINE/Contents/Resources/wine/bin/wine"
SERVER="\$ENGINE/Contents/Resources/wine/bin/wineserver"

note() { /usr/bin/osascript -e "display notification \"\$1\" with title \"Steam (FEX)\"" >/dev/null 2>&1; }

[ -x "\$WINE" ] || { note "Wine engine not found at \$ENGINE"; exit 1; }
[ -f "\$WINEPREFIX/drive_c/Program Files/Steam/steam.exe" ] || { note "Steam is not installed in \$WINEPREFIX"; exit 1; }

# already running in this prefix? then there is nothing to do
if /usr/bin/pgrep -f "steam.exe -cef-force-32bit" >/dev/null 2>&1; then
    note "Steam is already running."
    exit 0
fi

mkdir -p "\$HOME/Library/Logs"
"\$WINE" "C:\\\\Program Files\\\\Steam\\\\steam.exe" -cef-force-32bit -no-cef-sandbox -cef-disable-gpu -cef-in-process-gpu >>"\$LOG" 2>&1 &
WPID=\$!
trap '"\$SERVER" -k 2>/dev/null; kill \$WPID 2>/dev/null; exit 0' TERM INT HUP
wait \$WPID
LAUNCHER
chmod +x "$APP/Contents/MacOS/SteamFEX"

codesign --force -s - "$APP" 2>&1 | tail -1
echo "built: $APP"
