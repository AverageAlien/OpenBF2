#!/bin/sh
# Runs a Windows build command for OpenBF2 under Wine (MSVC from msvc-wine, Windows CMake + Ninja),
# from the repository root.
# Usage: winbuild.sh <command> [args...]   e.g. winbuild.sh cmake --build out/build/release-x86 --target bf2_client
# Settings (environment):
#   MSVC_WINEPREFIX  Wine prefix used for the build tools          default ~/src/wine-msvc
#   MSVC_DIR         msvc-wine install (vsdownload.py --dest)        default ~/src/msvc
#   WIN_TOOLS_DIR    folder with cmake-*-windows-x86_64/ and ninja/  default ~/src/tools
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
msvc_dir="${MSVC_DIR:-$HOME/src/msvc}"
tools_dir="${WIN_TOOLS_DIR:-$HOME/src/tools}"
cmake_bin=$(ls -d "$tools_dir"/cmake-*-windows-x86_64/bin 2>/dev/null | tail -n 1)
[ -d "$msvc_dir/vc/tools/msvc" ] || { echo "winbuild: no msvc-wine install in $msvc_dir (set MSVC_DIR)" >&2; exit 1; }
[ -n "$cmake_bin" ] || { echo "winbuild: no cmake-*-windows-x86_64 in $tools_dir (set WIN_TOOLS_DIR)" >&2; exit 1; }
[ -f "$tools_dir/ninja/ninja.exe" ] || { echo "winbuild: no ninja/ninja.exe in $tools_dir" >&2; exit 1; }

export WINEPREFIX="${MSVC_WINEPREFIX:-$HOME/src/wine-msvc}"
export WINEDEBUG="${WINEDEBUG:--all}"
# MSVC tools need their own vcruntime (msvc-wine uses =n); n,b lets built test programs fall back to Wine's
export WINEDLLOVERRIDES="vcruntime140=n,b;vcruntime140_1=n,b"
# Start Wine's background services detached from our stdout first: otherwise they inherit it and
# keep any pipe on this script's output open for minutes after the build ends.
wineserver -p 30 >/dev/null 2>&1
wine cmd /c exit >/dev/null 2>&1

# Wine passes these to winbuild.cmd and msvc-env.cmd as Windows paths
export OPENBF2_REPO_WIN="$(winepath -w "$repo")"
export MSVC_DIR_WIN="$(winepath -w "$msvc_dir")"
export WIN_TOOLS_PATH="$(winepath -w "$cmake_bin");$(winepath -w "$tools_dir/ninja")"
exec wine cmd /c "$(winepath -w "$here/winbuild.cmd")" "$@"
