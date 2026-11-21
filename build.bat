@echo off
setlocal

REM NativeDex build script for Visual Studio 2026
REM Generates and builds the project in the build/ directory.

cmake -S . -B build -G "Visual Studio 18 2026" -A x64
if errorlevel 1 (
    echo CMake configure failed.
    exit /b %errorlevel%
)

cmake --build build --config Release
if errorlevel 1 (
    echo Build failed.
    exit /b %errorlevel%
)

echo.
echo Build completed successfully.
pause
