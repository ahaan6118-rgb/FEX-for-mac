# fex-run profile for the Steam installer (SteamSetup.exe).
# After the first install nobody needs the installer again: if Steam is already there, just start Steam.
# The installer starts Steam itself without our settings, so after it finishes Steam is restarted through its profile.
MATCH_EXE="SteamSetup.exe"
SKIP_IF_EXISTS="drive_c/Program Files/Steam/steam.exe"
POST_RUN="drive_c/Program Files/Steam/steam.exe"
