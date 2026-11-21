@echo off
setlocal

rem Launcher for backend + dashboard
set BACKEND_EXE=build\Release\data_extraction.exe
set DASHBOARD_EXE=build-ui\data_extraction_dashboard.exe
set MOCK_BACKEND_EXE=build-ui\mock_dashboard_backend.exe
set DEFAULT_CONFIG=backend\config\config.json
set DEFAULT_WS=ws://localhost:8080/ws

if "%1"=="" (
    set CONFIG=%DEFAULT_CONFIG%
    set WS_URL=%DEFAULT_WS%
    echo Starting real backend and dashboard...
) else if /I "%1"=="mock" (
    echo Starting mock dashboard backend and dashboard...
    if not exist "%MOCK_BACKEND_EXE%" (
        echo Error: Mock dashboard backend not found: %MOCK_BACKEND_EXE%
        goto :EOF
    )
    if not exist "%DASHBOARD_EXE%" (
        echo Error: Dashboard executable not found: %DASHBOARD_EXE%
        goto :EOF
    )
    start "Mock Backend" "%MOCK_BACKEND_EXE%"
    start "Dashboard" "%DASHBOARD_EXE%" "%DEFAULT_CONFIG%" "%DEFAULT_WS%"
    goto :EOF
) else (
    set CONFIG=%1
    if "%2"=="" (
        set WS_URL=%DEFAULT_WS%
    ) else (
        set WS_URL=%2
    )
    echo Starting backend and dashboard with config: %CONFIG% and WebSocket URL: %WS_URL%
)

if not exist "%BACKEND_EXE%" (
    echo Error: Backend executable not found: %BACKEND_EXE%
    goto :EOF
)
if not exist "%DASHBOARD_EXE%" (
    echo Error: Dashboard executable not found: %DASHBOARD_EXE%
    goto :EOF
)
if not exist "%CONFIG%" (
    echo Error: Config file not found: %CONFIG%
    goto :EOF
)

start "Backend" "%BACKEND_EXE%" "%CONFIG%"
start "Dashboard" "%DASHBOARD_EXE%" "%CONFIG%" "%WS_URL%"

echo Launched backend and dashboard.
endlocal
