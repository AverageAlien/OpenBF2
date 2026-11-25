#!/bin/sh
# Runs the self-built OpenBF2 editor under Wine, with the game folder as working dir.
# Usage: run-openbf2-editor.sh [BF2Editor args...]   (default: +loadMod openbf2; +loadLevel <level> opens a level, +help lists the switches)
# The editor takes +loadMod <name>, not the client's +modPath. Same settings as run-openbf2.sh
# (BF2_GAME_DIR, BF2_WINEPREFIX, OPENBF2_BUILD).
# Wine output goes to $BF2_WINEPREFIX/debug/openbf2-editor-<timestamp>.log
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
[ -n "$BF2_GAME_DIR" ] || { echo "run-openbf2-editor: set BF2_GAME_DIR to the Battlefield 2 folder" >&2; exit 1; }
export WINEPREFIX="${BF2_WINEPREFIX:-$HOME/Games/bf2}"
exe="$repo/out/build/${OPENBF2_BUILD:-release-x86}/bin_editor/BF2Editor.exe"
[ -f "$exe" ] || { echo "run-openbf2-editor: $exe not built" >&2; exit 1; }
mkdir -p "$WINEPREFIX/debug"
log="$WINEPREFIX/debug/openbf2-editor-$(date +%Y%m%d-%H%M%S).log"
[ $# -eq 0 ] && set -- +loadMod openbf2
cd "$BF2_GAME_DIR" || exit 1
# Hyprland only: the editor never resizes its back buffer, so a window maximized later gets a stretched
# picture and a mouse that misses its buttons. So it starts at the maximized size (focused monitor minus
# the bar's reserved area, the Hyprland border on each side and Wine's 1-pixel frame on each side), and
# every editor window is maximized once when it appears. Switching the gameplay layer or opening another
# level starts the editor again as a new process (keeping +szx/+szy); the watch ends after 60 s without
# an editor window. On other desktops pass +szx <w> +szy <h> yourself.
if command -v hyprctl >/dev/null 2>&1 && command -v jq >/dev/null 2>&1 && hyprctl monitors >/dev/null 2>&1; then
    size=$(hyprctl monitors -j | jq -r --argjson b "$(hyprctl getoption general:border_size -j | jq .int)" \
        '.[] | select(.focused) | "\(.width - .reserved[0] - .reserved[2] - 2 * $b - 2) \(.height - .reserved[1] - .reserved[3] - 2 * $b - 2)"')
    case " $* " in
        *" +szx "*|*" +szy "*) ;;
        *) [ -n "$size" ] && set -- "$@" +szx "${size% *}" +szy "${size#* }" ;;
    esac
    (
        done_addrs=" "
        idle=0
        while [ $idle -lt 60 ]; do
            addrs=$(hyprctl clients -j | jq -r '.[] | select(.class == "bf2editor.exe" and (.title | startswith("BF2Editor - "))) | .address')
            [ -z "$addrs" ] && idle=$((idle + 1)) || idle=0
            for addr in $addrs; do
                case "$done_addrs" in *" $addr "*) continue ;; esac
                hyprctl dispatch "hl.dsp.window.fullscreen({ mode = \"maximized\", window = \"address:$addr\" })" > /dev/null
                done_addrs="$done_addrs$addr "
            done
            sleep 1
        done
    ) &
fi
# +debugstr logs OutputDebugString, where the rebuilt DLLs write their logs
WINEDEBUG="${WINEDEBUG:-fixme-all,+debugstr}" wine "$(winepath -w "$exe")" "$@" > "$log" 2>&1
echo "exit $? - log: $log"
