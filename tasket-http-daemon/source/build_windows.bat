@echo off
REM Build script for Tasket++ HTTP Trigger (Windows)
REM Requirements: Qt6 or Qt5, CMake 3.16+, MinGW or MSVC

echo ================================================
echo  Tasket++ HTTP Trigger — Windows Build Script
echo ================================================

set TASKETPP_ROOT=%~dp0..\tasketpp
set BUILD_DIR=%~dp0build
set CMAKE_PREFIX_PATH=C:\Qt\6.8.0\mingw_64

if not exist "%TASKETPP_ROOT%" (
    echo [ERROR] Tasket++ source not found at %TASKETPP_ROOT%
    echo Please clone: git clone https://github.com/AmirHammouteneEI/ScheduledPasteAndKeys.git tasketpp
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cd "%BUILD_DIR%"

cmake .. -G "MinGW Makefiles" ^
    -DCMAKE_PREFIX_PATH="%CMAKE_PREFIX_PATH%" ^
    -DTASKETPP_ROOT="%TASKETPP_ROOT%" ^
    -DCMAKE_BUILD_TYPE=Release

if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

cmake --build . --parallel

if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo [SUCCESS] Executable: %BUILD_DIR%\tasket-httpd.exe
echo.
echo To run:
echo   tasket-httpd.exe --port 7777 --bind 0.0.0.0 --dir ..\tasketpp\saved_tasks
echo.

pause
