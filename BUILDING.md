# Building GHOVLZ! DAW

This is a CMake project. There is no Projucer file and there is no JUCE module path to set. CMake downloads the libraries it is allowed to fetch (Catch2, nlohmann/json, RtMidi, miniaudio, stb, and the VST3 SDK). You do not install those by hand.

The ASIO SDK is not downloaded. Windows playback and recording use WASAPI. See the license note at the bottom if you later point a configure at an ASIO SDK you already have.

## What to install

1. **Visual Studio 2022**, with the workload **Desktop development with C++**. That provides the MSVC compiler, the Windows SDK (Direct2D, DirectWrite, D3D11, DXGI, WIC, WASAPI), and CMake if you tick it in the installer.
2. **CMake 3.24 or newer.** Visual Studio 2022 ships a new enough CMake. You can also install CMake from https://cmake.org/download/ and leave "Add CMake to the system PATH" on.
3. **Git**, from https://git-scm.com/download/win. Take the defaults so `git` works in a terminal.

Confirm the three tools in **x64 Native Tools Command Prompt for VS 2022** or in a normal terminal where those programs are on the PATH:

```bat
git --version
cmake --version
cl
```

`cl` prints the MSVC version. If `cl` is not found, open the x64 Native Tools prompt from the Start menu instead of a plain Command Prompt.

## Clone

```bat
git clone https://github.com/decksounds13/GHOVLZ-DAW.git
cd GHOVLZ-DAW
```

Use the real repository URL if it differs. The steps after the clone are the same.

## Configure

From the repository root:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

The first configure compiles nothing. It downloads dependencies and writes `build\GHOVLZ-DAW.sln`. Wait until it prints "Generating done".

## Open it in Visual Studio

Either of these works:

- **File > Open > Folder** and choose the repository root. Visual Studio uses the CMakeLists.txt directly. When it asks for a kit, choose the x64 kit.
- Or double-click `build\GHOVLZ-DAW.sln` (or use **File > Open > Project/Solution**).

The startup project is **GhovlzDAW**. CMake sets that. If Visual Studio starts a different target, right-click **GhovlzDAW** in Solution Explorer and choose **Set as Startup Project**.

## Build and run

In the toolbar set the configuration to **Release** and the platform to **x64**. Then **Build > Build Solution**, or press F5 to build and run.

The program you launch is:

```text
build\bin\RELEASE\GHOVLZ DAW.exe
```

That path is under the `build` directory you created with `-B build`. The folder name is `RELEASE` in capitals because the CMake output directories are set that way for every configuration.

Two VST3 bundles are built next to it for tests and the generic plugin window:

```text
build\VST3\Release\GHOVLZ Test Gain.vst3
build\VST3\Release\GHOVLZ Crash.vst3
```

Each `.vst3` path is a folder. The Windows binary inside the gain plugin is:

```text
build\VST3\Release\GHOVLZ Test Gain.vst3\Contents\x86_64-win\GHOVLZ Test Gain.vst3
```

The crash plugin is only there so the out-of-process scanner can prove it blacklists a plugin that aborts. Do not load it in a host you care about.

The test program and the scanner are:

```text
build\bin\RELEASE\GhovlzTests.exe
build\bin\RELEASE\GhovlzPluginScan.exe
```

From the build directory you can run the tests with:

```bat
ctest --test-dir build -C Release --output-on-failure
```

## Linux

The Linux build is for development and for the screenshot pass. It uses the same sources. Direct2D is Windows-only; Linux draws with the software canvas and opens an X11 window.

```bash
sudo apt-get install -y ninja-build pkg-config g++ cmake libasound2-dev libx11-dev libxext-dev xvfb fonts-liberation
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

The executable is:

```text
build/bin/RELEASE/GHOVLZ DAW
```

Screenshots of every main view:

```bash
"./build/bin/RELEASE/GHOVLZ DAW" --screenshot docs/screenshots
```

## Licenses that affect the build

- **VST3 SDK 3.8.1** (tag `v3.8.1_build_84`) is fetched by CMake. That tag is MIT, Copyright (c) 2026 Steinberg Media Technologies GmbH. This tree does not use the older 3.7 SDK, which is proprietary or GPL.
- **ASIO SDK** is optional and is not fetched. It is Steinberg's proprietary SDK for closed-source software. This repository does not contain it, and the app does not link it. Playback on Windows is WASAPI. If you already have the SDK, the hook is the `ASIO_SDK_DIR` cache variable; the current sources document that and still run on WASAPI. See `src/audio/AsioBackend.h` and `third_party/NOTICE.md`.
- This is an internal build. Do not redistribute the VST3 SDK or an ASIO-linked binary without checking Steinberg's terms.
