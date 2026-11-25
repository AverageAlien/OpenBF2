@echo off
rem Starts the self-built OpenBF2 client on Windows, with the game folder as working directory.
rem Usage: run-openbf2.cmd "<Battlefield 2 folder>" [BF2 args...]
rem Default args: +menu 1 +fullscreen 1 +modPath mods/openbf2. OPENBF2_BUILD picks the build folder
rem under out\build (default release-x86).
setlocal
set "GAME=%~1"
if "%GAME%"=="" (
    echo usage: run-openbf2.cmd "<Battlefield 2 folder>" [BF2 args...]
    exit /b 1
)
set "ARGS="
:args
shift
if "%~1"=="" goto run
set ARGS=%ARGS% %1
goto args
:run
if not defined ARGS set "ARGS=+menu 1 +fullscreen 1 +modPath mods/openbf2"
if not defined OPENBF2_BUILD set "OPENBF2_BUILD=release-x86"
set "EXE=%~dp0..\..\..\out\build\%OPENBF2_BUILD%\bin_client\BF2.exe"
if not exist "%EXE%" (
    echo run-openbf2: %EXE% not built
    exit /b 1
)
cd /d "%GAME%" || exit /b 1
start "" "%EXE%" %ARGS%
