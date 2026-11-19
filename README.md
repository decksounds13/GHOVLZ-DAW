# GHOVLZ! DAW

Decksounds GHOVLZ digital audio workstation. Version 0.1 is an internal C++20 build with no JUCE dependency.

Windows is the primary target. The UI renderer is Direct2D on a D3D11 device, with DirectWrite for text and a fallback to the WARP software device when no GPU is available. Drawing goes through a small canvas interface, so another backend can be added later. Linux uses that same interface with a software rasterizer so the app can be built, tested, and screenshotted without a GPU.

Audio I/O is WASAPI on Windows and ALSA on Linux, through miniaudio. Plugin hosting talks to the Steinberg VST3 SDK 3.8.1 directly. The optional ASIO SDK is not fetched and is not linked.

## Build

Exact clone, configure, and Visual Studio steps are in [BUILDING.md](BUILDING.md).

Linux, from a clean tree:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

The app binary is `build/bin/RELEASE/GHOVLZ DAW`. On Windows the same target is `build\bin\RELEASE\GHOVLZ DAW.exe`.

```bash
"./build/bin/RELEASE/GHOVLZ DAW" --screenshot docs/screenshots
```

## What v0.1 runs

- Transport, tempo, time signature, metronome, loop, locators
- Audio, MIDI, return, and master tracks, pre/post sends, plugin delay compensation
- Arrangement clips, session scenes, performance pads, mixer, device chain, piano roll, browser
- Native devices ported from the GHOVLZ EQ / DYN / analyzer work: EQ Eight, multiband dynamics, compressor, limiter, gate, saturator, filter, delay, reverb, chorus, flanger, phaser, utility, analyzer, wavetable, sampler, drum rack
- VST3 scan in a separate process, a blacklist, and in-process hosting of a plugin that loads
- Project folders (`project.json` plus 24-bit WAV). See [docs/PROJECT_FORMAT.md](docs/PROJECT_FORMAT.md)
- Offline WAV render

## Notes for the next JUCE-free apps

[docs/JUCE_PORTING_NOTES.md](docs/JUCE_PORTING_NOTES.md) is the running guide: what replaced each JUCE facility, the licenses, the DSP and UI recipes, and what is worth sharing as `ghovlz-core` / `ghovlz-ui`.

Screenshots and a concept comparison are in [docs/SCREENSHOT_AUDIT.md](docs/SCREENSHOT_AUDIT.md).

Third-party notices are in [third_party/NOTICE.md](third_party/NOTICE.md).
