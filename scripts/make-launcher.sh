#!/bin/bash
# Builds a double-clickable launcher app for one Windows program, running it through FEXforMac.app's fex-run
# (so it gets the same environment, profile and restart logic as the main app).
#
#   scripts/make-launcher.sh "App Name" "<program.exe or C:\path\program.exe>" [output-dir]
#   e.g.  scripts/make-launcher.sh "Steam (FEX)" 'C:\Program Files\Steam\steam.exe'
#
# The launcher points at FEXforMac.app by absolute path (rebuild it if you move the repository). Ad-hoc signed.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="${1:?usage: make-launcher.sh \"App Name\" \"program.exe\" [output-dir]}"
PROGRAM="${2:?usage: make-launcher.sh \"App Name\" \"program.exe\" [output-dir]}"
OUT="${3:-$ROOT}"
ENGINE="${FEX_APP:-$ROOT/FEXforMac.app}"
ID="com.fexformac.launcher.$(echo "$NAME" | tr -c 'A-Za-z0-9\n' '-' | tr '[:upper:]' '[:lower:]' | sed 's/-*$//')"
APP="$OUT/$NAME.app"

[ -x "$ENGINE/Contents/Resources/bin/fex-run" ] || "$ROOT/scripts/install-runner.sh" "$ENGINE"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>$NAME</string>
    <key>CFBundleDisplayName</key><string>$NAME</string>
    <key>CFBundleIdentifier</key><string>$ID</string>
    <key>CFBundleVersion</key><string>1.0</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundleExecutable</key><string>launch</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>LSMinimumSystemVersion</key><string>14.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
cat > "$APP/Contents/MacOS/launch" <<LAUNCH
#!/bin/bash
ENGINE="\${FEX_APP:-$ENGINE}"
export FEX_RUN_LOG="\$HOME/Library/Logs/FEXforMac.log"
exec "\$ENGINE/Contents/Resources/bin/fex-run" '$PROGRAM' "\$@"
LAUNCH
chmod +x "$APP/Contents/MacOS/launch"
codesign --force -s - "$APP" 2>&1 | tail -1
echo "built: $APP"
