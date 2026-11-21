@echo off
setlocal

set "BASE_DIR=%~dp0"
if exist "%BASE_DIR%data_extraction_dashboard.exe" (
    set "APP_EXE=%BASE_DIR%data_extraction_dashboard.exe"
    set "CONFIG_FILE=%BASE_DIR%config\config.json"
) else (
    set "APP_EXE=%BASE_DIR%build-ui\data_extraction_dashboard.exe"
    set "CONFIG_FILE=%BASE_DIR%config\config.json"
)

if not exist "%APP_EXE%" (
    echo Error: application executable not found:
    echo   %APP_EXE%
    exit /b 1
)

if not exist "%CONFIG_FILE%" (
    echo Error: config file not found:
    echo   %CONFIG_FILE%
    exit /b 1
)

set "STARTUP_DIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup"
set "STARTUP_BAT=%STARTUP_DIR%\DataExtractionDashboard_AutoStart.bat"

> "%STARTUP_BAT%" echo @echo off
>> "%STARTUP_BAT%" echo start "" /min "%APP_EXE%" "%CONFIG_FILE%"

echo Startup launcher created:
echo   %STARTUP_BAT%
echo The dashboard will start automatically for the current user at sign-in.

endlocal
