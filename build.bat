@echo off
:: =============================================================================
:: CursorOverlay v2.0 - MSVC Build Script (Visual Studio 2022/2025+)
:: =============================================================================
:: Open "x64 Native Tools Command Prompt for VS" and run this script.
:: =============================================================================

echo ============================================
echo CursorOverlay Build (MSVC)
echo ============================================

where cl.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: cl.exe not found. Open "x64 Native Tools Command Prompt for VS".
    exit /b 1
)

set SRC=src
set RES=res
set OUT=build
set TARGET=CursorOverlay.exe

if not exist %OUT% mkdir %OUT%

echo [1/3] Compiling resources...
rc /fo %OUT%\CursorOverlay.res %RES%\CursorOverlay.rc
if errorlevel 1 (
    echo ERROR: Resource compilation failed.
    exit /b 1
)

echo [2/3] Compiling main.cpp...
cl.exe /nologo /EHsc /O2 /W3 /permissive- /Zc:__cplusplus ^
    /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 ^
    /I%SRC% /Fo%OUT%\ /c %SRC%\main.cpp
if errorlevel 1 (
    echo ERROR: Compilation failed.
    exit /b 1
)

echo [3/3] Linking...
link.exe /nologo /SUBSYSTEM:WINDOWS /OUT:%OUT%\%TARGET% ^
    %OUT%\main.obj %OUT%\CursorOverlay.res ^
    user32.lib gdi32.lib shell32.lib shlwapi.lib gdiplus.lib
if errorlevel 1 (
    echo ERROR: Linking failed.
    exit /b 1
)

echo.
echo Build complete: %OUT%\%TARGET%
echo.
echo To run: start %OUT%\%TARGET%
