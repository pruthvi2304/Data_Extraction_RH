# Data Extraction Dashboard

A Qt 6 desktop application for monitoring industrial sensor controllers on Windows. The dashboard connects to controllers over TCP, parses framed humidity and temperature readings, displays device and sensor status, writes runtime logs, and can publish events to NATS.

## Requirements

- Windows 10 or later
- Qt 6 with the Core, Widgets, Charts, Network, and Sql modules
- CMake 3.20 or later
- A C++17 compiler supported by your Qt installation (the commands below use Qt MinGW)
- Optional: [nats.c](https://github.com/nats-io/nats.c) if building with native NATS publishing support
- Optional: Docker Compose to run the included local NATS server

## Build (Qt MinGW on Windows)

Adjust the Qt and MinGW paths for your installation:

```powershell
C:\Qt\Tools\CMake_64\bin\cmake.exe -S . -B build-ui -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.11.0\mingw_64" `
  -DCMAKE_C_COMPILER="C:\Qt\Tools\mingw1310_64\bin\gcc.exe" `
  -DCMAKE_CXX_COMPILER="C:\Qt\Tools\mingw1310_64\bin\g++.exe" `
  -DCMAKE_MAKE_PROGRAM="C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe"

C:\Qt\Tools\CMake_64\bin\cmake.exe --build build-ui --parallel
```

NATS support is optional at build time. To enable it, build/install `nats.c` and configure its source/build root when running CMake:

```powershell
cmake -S . -B build-ui -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.11.0\mingw_64" `
  -DNATS_DIR="C:\path\to\nats.c"
```

When NATS is unavailable, CMake builds the dashboard without native NATS publishing; the application can buffer events locally. The executable is created at `build-ui\data_extraction_dashboard.exe`.

## Configure and run

Review [config/config.json](config/config.json) and set the controller addresses, NATS URL, and certificate paths for your environment. The checked-in file is an example configuration; do not put private keys, NKey seeds, or production credentials in Git.

Run from the repository root so relative paths such as `logs` and `certs` resolve as expected:

```powershell
.\build-ui\data_extraction_dashboard.exe .\config\config.json
```

Runtime logs and local persistence are written under `logs\` (including `app.log`, `error.log`, `data.log`, the local publisher buffer, and the central SQLite database).

## Controller protocol

The dashboard uses plain TCP. Each controller sends an ASCII frame with a start byte (`STX`, `0x02`), humidity and temperature separated by a comma, and an end byte (`ETX`, `0x03`):

```text
<STX>37.3,32.5<ETX>
```

This example represents humidity `37.3` and temperature `32.5`.

### Test with the controller simulator

The Python simulator listens for a dashboard connection and sends a frame every five seconds by default:

```powershell
python .\tools\controller_stimulator.py
```

Defaults are `127.0.0.1:20108`, humidity `37.3`, and temperature `32.5`. Set a device in `config/config.json` to that address and port. To accept connections from another machine, bind to an appropriate interface and use the simulator host's reachable address in the dashboard config:

```powershell
python .\tools\controller_stimulator.py --host 0.0.0.0 --port 20108 --humidity 45.2 --temperature 28.6 --interval 5
```

Press `Ctrl+C` to stop the simulator.

## Local NATS server

The included `docker-compose.yml` starts NATS with JetStream enabled and persists its data in a Docker volume. It expects the configuration and certificate files referenced by `docker/nats/nats-server.conf` under `certs/nats/`.

```powershell
docker compose up -d plant-nats
```

NATS client TLS and NKey settings are configured in `config/config.json`. Keep all private keys and seed files private. The certificate generation helper is `tools/generate_nats_tls_nkey.py`; follow its prompts and the notes in `certs/nats/README.txt` when provisioning credentials.

## Package for deployment

After building, stage the Windows application and create a ZIP package:

```powershell
.\package_backend.ps1
```

The default output is `dist\data_extraction_dashboard_RH.zip`. The package script stages the executable, available Qt and MinGW runtime files, Qt plugins, config, certificate tree when present, log directory, startup helpers, and a generated `DEPLOYMENT.txt`. Review the staged configuration and credentials before sharing a package; it copies the certificate tree as-is.

Extract the ZIP on the target Windows machine, adjust `config\config.json` and required certificates, then run:

```powershell
.\run_dashboard.bat
```

Alternatively, run `data_extraction_dashboard.exe config\config.json` from the extracted folder.

## Repository layout

```text
config/       Runtime configuration
certs/        NATS server and publisher certificates (keep private material out of Git)
docker/nats/  Local NATS server configuration
logs/         Runtime logs and local persistence (generated)
tools/        Controller simulator and NATS credential helper
ui/           Qt application headers, sources, resources, and parser tests
CMakeLists.txt
package_backend.ps1
```
