@echo off
setlocal EnableDelayedExpansion

REM ==========================
REM NativeDex Launcher
REM ==========================

if "%~1"=="" (
    echo Usage: %~nx0 ^<apk^>
    exit /b 1
)

if not exist "logs" (
    mkdir "logs"
)

REM Obtener fecha y hora en formato seguro usando PowerShell
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd_HH-mm-ss"') do set TIMESTAMP=%%i

set LOGFILE=logs\%TIMESTAMP%.log

echo =============================================== > "%LOGFILE%"
echo NativeDex Launcher >> "%LOGFILE%"
echo Started: %DATE% %TIME% >> "%LOGFILE%"
echo APK: %~1 >> "%LOGFILE%"
echo =============================================== >> "%LOGFILE%"
echo. >> "%LOGFILE%"

echo Logging to: %LOGFILE%
echo.

.\build\launcher\Release\launcher.exe "%~1" >> "%LOGFILE%" 2>&1

echo. >> "%LOGFILE%"
echo =============================================== >> "%LOGFILE%"
echo Exit Code: %ERRORLEVEL% >> "%LOGFILE%"
echo Finished: %DATE% %TIME% >> "%LOGFILE%"
echo =============================================== >> "%LOGFILE%"

echo.
echo Done. Exit code: %ERRORLEVEL%
echo Log saved to %LOGFILE%

endlocal