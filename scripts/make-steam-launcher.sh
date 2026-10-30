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

sed "s#@ENGINE@#$ENGINE#" "$ROOT/scripts/SteamFEX.sh.in" > "$APP/Contents/MacOS/SteamFEX"
chmod +x "$APP/Contents/MacOS/SteamFEX"

codesign --force -s - "$APP" 2>&1 | tail -1
echo "built: $APP"
