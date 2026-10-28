#!/bin/bash
# Copies Homebrew GnuTLS and its dependencies into the app bundle's Frameworks directory and
# rewrites their install names so they resolve relative to each other (no Homebrew needed at
# runtime). Wine's schannel dlopen()s libgnutls.30.dylib; win32u.so/secur32.so rpath the Frameworks dir.
set -euo pipefail
APP="${1:?usage: bundle-gnutls.sh <FEXforMac.app>}"
FW="$APP/Contents/Frameworks"
mkdir -p "$FW"
LIBS=$(python3 - <<'PY'
import subprocess, os
seen = {}
todo = ['/opt/homebrew/opt/gnutls/lib/libgnutls.30.dylib']
while todo:
    p = todo.pop()
    if os.path.basename(p) in seen: continue
    seen[os.path.basename(p)] = p
    out = subprocess.check_output(['otool', '-L', os.path.realpath(p)]).decode().splitlines()[1:]
    todo += [l.split()[0] for l in out if l.strip().startswith('/opt/homebrew')]
print('\n'.join(seen.values()))
# libraries are referenced by their install names (e.g. libhogweed.7.dylib), not realpath names
PY
)
for src in $LIBS; do
    name=$(basename "$src")
    cp -f "$src" "$FW/$name"; chmod u+w "$FW/$name"
    install_name_tool -id "@loader_path/$name" "$FW/$name"
done
for src in $LIBS; do
    name=$(basename "$src")
    for dep in $(otool -L "$FW/$name" | tail -n +2 | awk '{print $1}' | grep '^/opt/homebrew' || true); do
        install_name_tool -change "$dep" "@loader_path/$(basename "$dep")" "$FW/$name"
    done
    codesign -f -s - "$FW/$name"
done
# secur32.so must be linked with -Wl,-rpath,@loader_path/../../../../../Frameworks (see build notes);
# install_name_tool cannot add it afterwards because the header has no spare room.
echo "bundled: $(echo "$LIBS" | wc -l | tr -d ' ') libraries"
