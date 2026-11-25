@echo off
rem Stand-in for VsDevCmd.bat with the msvc-wine install in MSVC_DIR_WIN (VsDevCmd can't find the
rem Windows SDK under Wine). Accepts the same "-arch=x86|x64" argument; extra arguments are ignored.
rem Paths mirror <msvc-wine>/bin/<arch>/msvcenv.sh; the newest MSVC and SDK versions installed are used.
set "MSVC_ARCH=%~1"
if /i "%MSVC_ARCH%"=="-arch" set "MSVC_ARCH=%~2"
if /i "%MSVC_ARCH:~0,6%"=="-arch=" set "MSVC_ARCH=%MSVC_ARCH:~6%"
if not defined MSVC_ARCH set "MSVC_ARCH=x86"
if not defined MSVC_DIR_WIN (
    echo [msvc-env] MSVC_DIR_WIN is not set
    exit /b 1
)
for /d %%d in ("%MSVC_DIR_WIN%\vc\tools\msvc\*") do set "VCToolsVersion=%%~nxd"
set "MSVCDIR=%MSVC_DIR_WIN%\vc\tools\msvc\%VCToolsVersion%"
set "SDKDIR=%MSVC_DIR_WIN%\kits\10"
for /d %%d in ("%SDKDIR%\include\10.*") do set "SDKVER=%%~nxd"
set "INCLUDE=%MSVCDIR%\atlmfc\include;%MSVCDIR%\include;%SDKDIR%\include\%SDKVER%\shared;%SDKDIR%\include\%SDKVER%\ucrt;%SDKDIR%\include\%SDKVER%\um;%SDKDIR%\include\%SDKVER%\winrt"
set "LIB=%MSVCDIR%\atlmfc\lib\%MSVC_ARCH%;%MSVCDIR%\lib\%MSVC_ARCH%;%SDKDIR%\lib\%SDKVER%\ucrt\%MSVC_ARCH%;%SDKDIR%\lib\%SDKVER%\um\%MSVC_ARCH%"
set "LIBPATH=%LIB%"
set "PATH=%MSVCDIR%\bin\Hostx64\%MSVC_ARCH%;%SDKDIR%\bin\%SDKVER%\x64;%MSVCDIR%\bin\Hostx64\x64;%PATH%"
set "VSCMD_ARG_TGT_ARCH=%MSVC_ARCH%"
set "WindowsSDKVersion=%SDKVER%\"
exit /b 0
