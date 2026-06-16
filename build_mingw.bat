@echo off
:: =============================================================================
:: CursorOverlay v2.0 - MinGW-w64 Build Script
:: =============================================================================
:: Requires MinGW-w64 in PATH (e.g., MSYS2: pacman -S mingw-w64-x86_64-gcc)
:: =============================================================================

echo ============================================
echo CursorOverlay Build (MinGW)
echo ============================================

where g++.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: g++.exe not found. Install MinGW-w64 and add to PATH.
    exit /b 1
)
where windres.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: windres.exe not found.
    exit /b 1
)

set SRC=src
set RES=res
set OUT=build_mingw
set TARGET=CursorOverlay.exe

if not exist %OUT% mkdir %OUT%

echo [1/3] Compiling resources...
windres -i %RES%\CursorOverlay.rc -o %OUT%\CursorOverlay_res.o
if errorlevel 1 (
    echo ERROR: Resource compilation failed.
    exit /b 1
)

echo [2/3] Compiling main.cpp...
g++ -std=c++17 -O2 -Wall -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 ^
    -I%SRC% -c %SRC%\main.cpp -o %OUT%\main.o
if errorlevel 1 (
    echo ERROR: Compilation failed.
    exit /b 1
)

echo [3/3] Linking...
g++ -o %OUT%\%TARGET% %OUT%\main.o %OUT%\CursorOverlay_res.o ^
    -static-libgcc -static-libstdc++ ^
    -luser32 -lgdi32 -lshell32 -lshlwapi -lgdiplus -luuid -mwindows
if errorlevel 1 (
    echo ERROR: Linking failed.
    exit /b 1
)

echo.
echo Build complete: %OUT%\%TARGET%
echo.
echo To run: start %OUT%\%TARGET%
