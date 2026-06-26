@echo off
REM Build script for Tasket++ HTTP Trigger (Windows)
REM Requirements: Qt6, CMake, Visual Studio C++ toolchain

echo ================================================
echo  Tasket++ HTTP Trigger - Windows Build Script
echo ================================================
echo.
echo Preferred fresh-machine path:
echo   pwsh .\build.ps1 -DryRun
echo   pwsh .\build.ps1
echo.

set TASKETPP_ROOT=%~dp0original
set BUILD_DIR=%~dp0build-msvc
if "%CMAKE_PREFIX_PATH%"=="" (
    echo [ERROR] CMAKE_PREFIX_PATH is not set.
    echo Set it to your Qt6 MSVC prefix, for example:
    echo   set CMAKE_PREFIX_PATH=C:\Qt\6.8.4\msvc2022_64
    echo Or use the PowerShell fresh-machine script above.
    exit /b 1
)

if not exist "%TASKETPP_ROOT%" (
    echo [ERROR] Tasket++ source not found at %TASKETPP_ROOT%
    echo Please clone: git clone https://github.com/AmirHammouteneEI/ScheduledPasteAndKeys.git original
    echo Then apply: git -C original apply patches\Task.h.patch
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cd "%BUILD_DIR%"

cmake .. -G "Visual Studio 18 2026" -A x64 ^
    -DCMAKE_PREFIX_PATH="%CMAKE_PREFIX_PATH%" ^
    -DTASKETPP_ROOT="%TASKETPP_ROOT%" ^
    -DTASKET_HTTP_BIND=127.0.0.1

if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

cmake --build . --config Release --parallel

if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo [SUCCESS] Executable: %BUILD_DIR%\Release\tasket-httpd.exe
echo.
echo To run:
echo   tasket-httpd.exe --port 7777 --bind 127.0.0.1 --dir %%APPDATA%%\Tasket++\saved_tasks
echo.

pause
