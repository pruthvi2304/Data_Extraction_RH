<#
.SYNOPSIS
Packages the Qt dashboard application into a production-ready deployment ZIP.

.DESCRIPTION
Creates a clean deployment folder containing the dashboard executable, Qt runtime DLLs,
MinGW runtime DLLs, required Qt plugin folders, config, logs folder, and a launch script.
Optionally runs windeployqt against the staged executable before zipping everything under dist\.
#>

param(
    [string]$BuildDir = ".\build-ui",
    [string]$ExecutableName = "data_extraction_dashboard.exe",
    [string]$ConfigFile = ".\config\config.json",
    [string]$OutputDir = ".\dist\data_extraction_dashboard",
    [string]$PackageName = "data_extraction_dashboard_RH.zip",
    [string]$WindeployQtPath = "C:\Qt\6.11.0\mingw_64\bin\windeployqt.exe",
    [string]$OpenSslDir = "C:\Program Files\OpenSSL-Win64\bin"
)

$ErrorActionPreference = 'Stop'

function Copy-OptionalDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    if (Test-Path -LiteralPath $Source) {
        Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
    }
}

function Find-OpenSslBinDirectory {
    param(
        [string]$OverridePath
    )

    $searchPaths = @()
    if ($OverridePath) {
        $searchPaths += $OverridePath
    }
    $searchPaths += @(
        "$env:ProgramFiles\OpenSSL-Win64\bin",
        "$env:ProgramFiles\OpenSSL-Win64",
        "$env:ProgramFiles(x86)\OpenSSL-Win32\bin",
        "$env:ProgramFiles(x86)\OpenSSL-Win32",
        "C:\OpenSSL-Win64\bin",
        "C:\OpenSSL-Win64",
        "C:\OpenSSL-Win32\bin",
        "C:\OpenSSL-Win32",
        "C:\Qt\Tools\mingw1310_64\opt\bin"
    )

    foreach ($path in $searchPaths | Get-Unique) {
        if ($path -and (Test-Path -LiteralPath $path)) {
            $ssl = Join-Path $path "libssl-4-x64.dll"
            $crypto = Join-Path $path "libcrypto-4-x64.dll"
            if ((Test-Path -LiteralPath $ssl) -and (Test-Path -LiteralPath $crypto)) {
                return $path
            }
        }
    }

    return $null
}

function Copy-OpenSslLibraries {
    param(
        [string]$Destination
    )

    $opensslBin = Find-OpenSslBinDirectory -OverridePath $OpenSslDir
    if (-not $opensslBin) {
        Write-Warning "OpenSSL runtime not found. If Qt Network requires TLS, the deployed app may fail with libssl-4-x64.dll or libcrypto-4-x64.dll missing."
        return
    }

    foreach ($dll in @('libssl-4-x64.dll', 'libcrypto-4-x64.dll')) {
        $source = Join-Path $opensslBin $dll
        if (Test-Path -LiteralPath $source) {
            Write-Host "Copying OpenSSL runtime: $dll from $opensslBin" -ForegroundColor Green
            Copy-Item -LiteralPath $source -Destination $Destination -Force
        }
    }
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDirFull = Join-Path $scriptRoot $BuildDir
$configFileFull = Join-Path $scriptRoot $ConfigFile
$outputDirFull = Join-Path $scriptRoot $OutputDir
$packageDir = Join-Path $scriptRoot "dist"
$packagePath = Join-Path $packageDir $PackageName
$exePath = Join-Path $buildDirFull $ExecutableName
$configDir = Join-Path $outputDirFull "config"
$logsDir = Join-Path $outputDirFull "logs"
$startupInstall = Join-Path $scriptRoot "install_startup.bat"
$startupRemove = Join-Path $scriptRoot "remove_startup.bat"

$runningProcesses = Get-Process -Name ([System.IO.Path]::GetFileNameWithoutExtension($ExecutableName)) -ErrorAction SilentlyContinue
if ($runningProcesses) {
    throw "Close all running instances of $ExecutableName before packaging."
}

if (-not (Test-Path -LiteralPath $buildDirFull)) {
    throw "Build directory not found: $buildDirFull"
}

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Application executable not found: $exePath. Build the project first."
}

if (-not (Test-Path -LiteralPath $configFileFull)) {
    throw "Config file not found: $configFileFull"
}

Write-Host "Creating deployable dashboard package..." -ForegroundColor Cyan
Write-Host "  Build directory : $buildDirFull"
Write-Host "  Executable      : $exePath"
Write-Host "  Config          : $configFileFull"
Write-Host "  Output folder   : $outputDirFull"
Write-Host "  Package         : $packagePath"

Remove-Item -LiteralPath $outputDirFull -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $outputDirFull -Force | Out-Null
New-Item -ItemType Directory -Path $configDir -Force | Out-Null
New-Item -ItemType Directory -Path $logsDir -Force | Out-Null

Copy-Item -LiteralPath $exePath -Destination $outputDirFull -Force

Get-ChildItem -LiteralPath $buildDirFull -File | Where-Object {
    $_.Extension -in @(".dll")
} | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $outputDirFull -Force
}

# Copy NATS library if available
$natsLibPaths = @(
    "nats.dll",
    "libnats.dll",
    "C:/nats.c/build/bin/Release/nats.dll",
    "$env:NATS_DIR/build/bin/Release/nats.dll",
    "$env:NATS_DIR/bin/nats.dll"
)

foreach ($natsLib in $natsLibPaths) {
    if ($natsLib -and (Test-Path -LiteralPath $natsLib)) {
        Write-Host "Copying NATS library: $natsLib" -ForegroundColor Green
        Copy-Item -LiteralPath $natsLib -Destination $outputDirFull -Force
        break
    }
}

$pluginDirs = @(
    "platforms",
    "imageformats",
    "iconengines",
    "generic",
    "networkinformation",
    "styles",
    "tls",
    "translations"
)

foreach ($dir in $pluginDirs) {
    Copy-OptionalDirectory -Source (Join-Path $buildDirFull $dir) -Destination $outputDirFull
}

$stagedExe = Join-Path $outputDirFull $ExecutableName
if (Test-Path -LiteralPath $WindeployQtPath) {
    Write-Host "Running windeployqt on staged executable..." -ForegroundColor Cyan
    & $WindeployQtPath --force --dir $outputDirFull $stagedExe
    Start-Sleep -Seconds 2
    Copy-OpenSslLibraries -Destination $outputDirFull
} else {
    Write-Warning "windeployqt not found at $WindeployQtPath. Packaging existing runtime files only."
    Copy-OpenSslLibraries -Destination $outputDirFull
}

Copy-Item -LiteralPath $configFileFull -Destination (Join-Path $configDir "config.json") -Force
if (Test-Path -LiteralPath $startupInstall) {
    Copy-Item -LiteralPath $startupInstall -Destination (Join-Path $outputDirFull "install_startup.bat") -Force
}
if (Test-Path -LiteralPath $startupRemove) {
    Copy-Item -LiteralPath $startupRemove -Destination (Join-Path $outputDirFull "remove_startup.bat") -Force
}

# Copy the complete certificate tree, including client credentials and the
# NATS CA certificate when it is present.
$certSource = Join-Path $scriptRoot "certs"
$certDestination = Join-Path $outputDirFull "certs"
Copy-OptionalDirectory -Source $certSource -Destination $certDestination

$runBat = @"
@echo off
setlocal
set APP_DIR=%~dp0
"%APP_DIR%$ExecutableName" "%APP_DIR%config\config.json"
endlocal
"@
Set-Content -LiteralPath (Join-Path $outputDirFull "run_dashboard.bat") -Value $runBat -Encoding ASCII

$readme = @"
Deployment package contents:
- $ExecutableName
- Qt and MinGW runtime DLLs
- NATS library DLL (if compiled with NATS support)
- Qt plugin folders
- config\config.json
- logs\ (created for runtime logs)
- run_dashboard.bat
- install_startup.bat
- remove_startup.bat

Prerequisites for NATS connectivity:
- NATS server running and accessible (configured in config.json)
- libnats.dll included in package (automatically copied during build)
- certs/ included in package if present (preserves publisher and NATS CA paths)

Run on another machine:
1. Extract the ZIP to any writable folder.
2. Update config\config.json if device IP/port or NATS server differs.
3. If TLS is used, verify the configured client and CA certificate files are present under certs\.
4. Double-click run_dashboard.bat or run:
   $ExecutableName config\config.json
5. To auto-start at sign-in, run install_startup.bat once.
"@
Set-Content -LiteralPath (Join-Path $outputDirFull "DEPLOYMENT.txt") -Value $readme -Encoding ASCII

if (-not (Test-Path -LiteralPath $packageDir)) {
    New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
}

if (Test-Path -LiteralPath $packagePath) {
    Remove-Item -LiteralPath $packagePath -Force
}

Compress-Archive -Path (Join-Path $outputDirFull '*') -DestinationPath $packagePath -Force

Write-Host "Deployable package created successfully:" -ForegroundColor Green
Write-Host "  $packagePath"
