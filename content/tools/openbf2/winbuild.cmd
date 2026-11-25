@echo off
rem Runs a command inside the MSVC x86 developer environment (msvc-wine), from the OpenBF2 repo root.
rem Called by winbuild.sh, which sets OPENBF2_REPO_WIN, MSVC_DIR_WIN and WIN_TOOLS_PATH.
rem Usage: winbuild.cmd <command> [args...]
set "BF2_VSDEVCMD=%~dp0msvc-env.cmd"
call "%BF2_VSDEVCMD%" -arch=x86 -no_logo
if errorlevel 1 (
    echo [winbuild] msvc-env.cmd failed
    exit /b 1
)
set "PATH=%WIN_TOOLS_PATH%;%PATH%"
cd /d "%OPENBF2_REPO_WIN%"
%*
