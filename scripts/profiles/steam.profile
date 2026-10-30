# fex-run profile for the Steam client (steam.exe).
# Plain shell variables, sourced by fex-run. MATCH_EXE is compared with the program's file name.
MATCH_EXE="steam.exe"

# 32-bit Chromium (steamwebhelper) under emulation: force the 32-bit build, no sandbox, software compositing
# inside the browser process (Wine on macOS cannot show a window another process paints into).
EXTRA_ARGS="-cef-force-32bit -no-cef-sandbox -cef-disable-gpu -cef-in-process-gpu"

# one Steam per prefix
SINGLE_INSTANCE_PATTERN="steam.exe -cef-force-32bit"

# Steam's Chromium sometimes freezes at startup. Its log (relative to the prefix) then contains "commit hung",
# or no renderer process ever starts. Restart up to TRIES times; a program that exits within EARLY_EXIT_SECS
# counts as a startup crash.
WATCH_LOG="drive_c/Program Files/Steam/logs/cef_log.txt"
HANG_PATTERN="commit hung"
# healthy = some Steam window was actually shown ("WasHidden 0" in the client log; a login window that is created
# but never shown is another form of the same freeze)
OK_LOG="drive_c/Program Files/Steam/logs/webhelper.txt"
OK_PATTERN="WasHidden 0"
SETTLE=110
TRIES=4
EARLY_EXIT_SECS=60

# No DXMT d3d11/dxgi override: Chromium probes Direct3D at startup and that probe makes Steam start ~2x slower.
DLLOVERRIDES="winemetal=b"

# After startup, a new browser renderer can freeze too (same "commit hung" log line). Killing the newest renderer
# makes Steam respawn it and the UI comes back.
RECOVER_KILL="steamwebhelper.exe.*--type=renderer"

# Closing the app (or Stop) first asks Steam to exit by itself so it can save your login and settings.
QUIT_ARGS="-shutdown"

# Steam exits with 42 when it restarts itself (after an update check or an unclean shutdown); keep following it.
RESTART_CODE=42
