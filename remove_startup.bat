@echo off
setlocal

set "STARTUP_BAT=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\DataExtractionDashboard_AutoStart.bat"

if exist "%STARTUP_BAT%" (
    del /f /q "%STARTUP_BAT%"
    echo Removed startup launcher:
    echo   %STARTUP_BAT%
) else (
    echo Startup launcher not found:
    echo   %STARTUP_BAT%
)

endlocal
