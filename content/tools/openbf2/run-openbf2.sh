#!/bin/sh
# Runs the self-built OpenBF2 client under Wine, with the game folder as working dir.
# Usage: run-openbf2.sh [BF2 args...]   (default: +menu 1 +fullscreen 1 +modPath mods/openbf2; windowed mode may draw no terrain)
# Settings (environment):
#   BF2_GAME_DIR    retail Battlefield 2 folder (holds BF2.exe and mods/)   required
#   BF2_WINEPREFIX  Wine prefix the game is installed in                    default ~/Games/bf2
#   OPENBF2_BUILD   build folder under out/build                            default release-x86
# Wine output goes to $BF2_WINEPREFIX/debug/openbf2-<timestamp>.log
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
[ -n "$BF2_GAME_DIR" ] || { echo "run-openbf2: set BF2_GAME_DIR to the Battlefield 2 folder" >&2; exit 1; }
export WINEPREFIX="${BF2_WINEPREFIX:-$HOME/Games/bf2}"
exe="$repo/out/build/${OPENBF2_BUILD:-release-x86}/bin_client/BF2.exe"
[ -f "$exe" ] || { echo "run-openbf2: $exe not built" >&2; exit 1; }
mkdir -p "$WINEPREFIX/debug"
log="$WINEPREFIX/debug/openbf2-$(date +%Y%m%d-%H%M%S).log"
[ $# -eq 0 ] && set -- +menu 1 +fullscreen 1 +modPath mods/openbf2
cd "$BF2_GAME_DIR" || exit 1
# DXVK_HUD=fps shows the frame rate (OpenBF2 has no renderer.drawFps); DXVK_HUD= turns it off
# vcruntime140=n,b: a Microsoft vcruntime140.dll beside BF2.exe wins over Wine's builtin when present
# +debugstr logs OutputDebugString, where the rebuilt DLLs and the sound engine report errors
DXVK_HUD="${DXVK_HUD-fps}" WINEDLLOVERRIDES="${WINEDLLOVERRIDES-vcruntime140=n,b}" WINEDEBUG="${WINEDEBUG:-fixme-all,+debugstr}" \
    wine "$(winepath -w "$exe")" "$@" > "$log" 2>&1
echo "exit $? - log: $log"
