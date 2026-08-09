@echo off
rem Build SmNative.dll (x86_64) with clang from MSYS2 CLANG64.
rem By ayanami770 - (c) 2026 - Available under the CDDL-1.0 license
setlocal
set CLANG=C:\msys64\clang64\bin\clang.exe
if not exist "%CLANG%" (
    echo clang not found at %CLANG% - install with: pacman -S mingw-w64-clang-x86_64-clang
    exit /b 1
)
cd /d "%~dp0"

echo Building SmNative.dll...
"%CLANG%" -std=c99 -O2 -Wall -Wextra -DSMNATIVE_BUILD -shared ^
    -o SmNative.dll crc32.c inflate.c deflate.c zip.c audio.c ^
    -lole32 -Wl,--no-undefined
if errorlevel 1 exit /b 1

echo Building test.exe...
"%CLANG%" -std=c99 -O2 -Wall -Wextra -DSMNATIVE_BUILD -municode ^
    -o test.exe test.c crc32.c inflate.c deflate.c zip.c audio.c ^
    -lole32
if errorlevel 1 exit /b 1

echo Copying SmNative.dll to SoundManager project...
copy /y SmNative.dll ..\SoundManager\SmNative.dll >nul
echo Done.
