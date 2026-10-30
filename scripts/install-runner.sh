#!/bin/bash
# Installs the generic runner (fex-run), its profiles and the prefix fixes into FEXforMac.app.
#   scripts/install-runner.sh [path/to/FEXforMac.app]      (default: repo root FEXforMac.app)
# Also called by assemble-vkmt-bundle.sh. Idempotent.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="${1:-${FEX_APP:-$ROOT/FEXforMac.app}}"
RES="$APP/Contents/Resources"
[ -d "$RES" ] || { echo "ERROR: $APP is not an app bundle" >&2; exit 1; }
mkdir -p "$RES/bin" "$RES/profiles" "$RES/scripts"
cp "$ROOT/scripts/fex-run" "$RES/bin/fex-run"
cp "$ROOT/scripts/prefix-extras.sh" "$RES/scripts/prefix-extras.sh"
cp "$ROOT"/scripts/profiles/*.profile "$RES/profiles/"
chmod +x "$RES/bin/fex-run" "$RES/scripts/prefix-extras.sh"
echo "installed fex-run + $(ls "$RES/profiles" | wc -l | tr -d ' ') profile(s) into $APP"
