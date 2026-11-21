# Build Guide

This project is configured with CMake and is primarily targeted at Windows. The root build script is defined in `CMakeLists.txt`, and the project includes runtime modules, Android framework shims, platform abstraction code, launcher, tools, and tests.

## Supported environment

- Windows 10/11 recommended
- CMake 3.20 or newer
- C++20-capable compiler
- Visual Studio 2022 with C++ workload is the most natural setup
- Ninja is also usable if preferred

> NativeDex is currently an experimental project. The build is meant to be used mainly for local development and testing of the runtime skeleton and early components.

## Current build state (important)

The launcher layer is unfinished: `launcher/main.cpp`, `launcher/ui/ActivityManager.*` and
`launcher/ui/NativeBridge.cpp` still need to be adapted to the runtime API (see `STATUS.md` and
`ROADMAP.md`), so **the `launcher` target does not compile yet**. The `runtime/` libraries build with
GCC on Linux; they have not been built with MSVC yet.

When the launcher links, add `/STACK:268435456` (for example with `target_link_options`) to the
executable: the interpreter recurses on the native stack and deep Java call chains need it.

## Linux harness (runtime only)

The platform-independent runtime builds on Linux with g++ (C++20) and can run real DEX files without
any UI:

```sh
# extract the DEX files of an APK
python3 - <<'PY'
import zipfile, os
os.makedirs('/tmp/dex', exist_ok=True)
z = zipfile.ZipFile('path/to/app.apk')
[open('/tmp/dex/' + n, 'wb').write(z.read(n)) for n in z.namelist()
 if n.startswith('classes') and n.endswith('.dex')]
PY

# build and run: <Activity descriptor> [<Application descriptor> | -] [debug]
tools/linux_harness/build_and_run.sh "Lcom/example/MainActivity;" "Lcom/example/App;" debug
```

`debug` prints every invocation. `logs/native_bridge_missing.log` lists framework methods the app
called that are not implemented yet. Pass the app's `Application` class (see its manifest) so it is
started before the Activity, as Android does; use `-` if the app has none.

## Repository layout relevant to the build

- `CMakeLists.txt` — root build definition
- `runtime/**/CMakeLists.txt` — runtime subsystem libraries
- `android/**/CMakeLists.txt` — Android API layer libraries
- `platform/windows/CMakeLists.txt` — Windows platform implementation
- `launcher/CMakeLists.txt` — launcher executable
- `tests/CMakeLists.txt` — unit test target
- `third_party/miniz` — bundled dependency

## Prerequisites

### Windows

Install:

- Visual Studio 2022
- Desktop development with C++ workload
- CMake

### Optional

- Git
- A configured terminal with access to `cmake` and `cl`/MSBuild

## Configure the project

From the repository root:

```powershell
mkdir build
cmake -S . -B build -G "Visual Studio 17 2022" -DBUILD_TESTS=ON -DBUILD_TOOLS=ON
```

If you prefer Ninja:

```powershell
cmake -S . -B build -G "Ninja" -DBUILD_TESTS=ON -DBUILD_TOOLS=ON
```

### Configuration options

The root CMake file exposes these options:

- `BUILD_TESTS` = `ON` by default
- `BUILD_TOOLS` = `ON` by default

Example:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -DBUILD_TESTS=OFF -DBUILD_TOOLS=ON
```

## Build

### Visual Studio / MSBuild

```powershell
cmake --build build --config Release
```

### Ninja

```powershell
cmake --build build
```

This will build the project libraries and the `launcher` executable.

## Run the launcher

The project includes a helper script:

```powershell
.
launcher.bat C:\path\to\app.apk
```

The script expects the compiled launcher binary under:

```text
build\launcher\Release\launcher.exe
```

You can also run it manually:

```powershell
.\build\launcher\Release\launcher.exe C:\path\to\app.apk
```

## Run tests

The test target is configured in `tests/CMakeLists.txt` and can be run through CTest or directly from the build output.

Example with CTest:

```powershell
ctest --test-dir build --build-config Release --output-on-failure
```

Or build the target directly:

```powershell
cmake --build build --config Release --target run_tests
```

## Common build notes

### 1. Windows-specific code

The `platform/windows` target is a primary dependency, and the project is built around Win32 APIs, Direct2D, Win32 threads, and window management. This is not a cross-platform build-ready project yet.

### 2. MSVC compiler flags

The root CMake file applies stricter warning settings for MSVC and also sanitizes debug flags to avoid some MSVC-specific `.pdb` issues. The project is configured to compile with C++20.

### 3. Linux/macOS directories are present but not the main build target

The project tree contains `platform/linux` and `platform/macos`, but the active build path is currently centered on Windows. If you are building outside Windows, you should expect incomplete support or missing platform-specific implementations.

## Typical development flow

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -DBUILD_TESTS=ON -DBUILD_TOOLS=ON
cmake --build build --config Release
ctest --test-dir build --build-config Release --output-on-failure
```

## Troubleshooting

### cmake cannot find a compiler

Use a Visual Studio Developer Command Prompt or make sure your environment has the C++ toolchain installed.

### build fails due to missing Windows SDK

Install the Windows 10/11 SDK from the Visual Studio installer.

### launcher script points to the wrong binary

Check that the output path matches the actual generated binary location. In a single-config generator such as Ninja, the output may differ from the Visual Studio multi-config path.

## References

- [README.md](README.md)
- [ARCHITECTURE.md](ARCHITECTURE.md)
- [CMakeLists.txt](CMakeLists.txt)
