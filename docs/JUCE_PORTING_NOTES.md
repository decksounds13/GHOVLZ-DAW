# JUCE porting notes

GHOVLZ DAW is the first GHOVLZ app built without JUCE. These notes are the map for EQ, Analyzer, DYN, Terrain, Vegetation, Fluid, and Scan if those move to the same stack. They describe what this tree actually did.

A source gate rejects the old framework's type names, module headers, Projucer files, and CMake package calls. `tools/check_no_juce.py` is the CI check. `cmake/NoJuce.cmake` is the CTest equivalent named `no_juce`. Documentation may name the old framework. Source, plugins, the scanner, tests, and CMake may not.

## 1. Replacement map

| Old facility | Replacement in this tree | License |
| --- | --- | --- |
| `AudioBuffer` / `AudioBuffer<float>` | `ghovlz::AudioBuffer`, planar float, channel `c` at `c * frames` | Project code |
| `MidiBuffer` / `MidiMessage` | `MidiEvent` + `MidiBuffer` | Project code |
| `AudioProcessor` / `AudioProcessorGraph` | `Device` and `Engine` | Project code |
| `dsp::IIR` / `dsp::ProcessorDuplicator` | `Biquad` in `src/dsp/Biquad.h`, Audio EQ Cookbook, transposed DF2 | Project code. Cookbook is RBJ's published formulas |
| `dsp::FFT` | `fftRadix2` in `src/dsp/Fft.h` | Project code |
| `AudioProcessorValueTreeState`, `AudioParameterFloat` | `Param` (`id`, `min`, `max`, `skew`, choices) and `DeviceState.params` | Project code |
| `ValueTree` / `.xml` projects | Folder format in `docs/PROJECT_FORMAT.md`, nlohmann/json 3.11.3 | MIT |
| `AudioDeviceManager`, WASAPI, ASIO wrappers | miniaudio 0.11.22. WASAPI on Windows, ALSA on Linux | Public domain or MIT-0 |
| ASIO | Not linked. Proprietary Steinberg ASIO SDK, optional later via `ASIO_SDK_DIR`. GPLv3 variant not used | Proprietary, not bundled |
| `VST3PluginFormat`, `AudioPluginFormatManager` | VST3 SDK 3.8.1 (`v3.8.1_build_84`) hosting classes, plus `PluginHost` | MIT (this tag). 3.7 is not used |
| Out-of-process scan | `GhovlzPluginScan` child process. Non-zero exit appends the path to a blacklist file | Project code |
| `MessageManager` / timer | The platform loop (Win32 or X11) paints and polls MIDI. The audio callback only calls `Engine::process` | Project code |
| `LookAndFeel`, `Component` | `Canvas` + `paintFrame`. No widget toolkit | Project code |
| Direct2D renderer inside the old framework | `Direct2DCanvas`: D3D11 device, DXGI flip-model swap chain, Direct2D 1.1 device context, DirectWrite | Windows SDK |
| Software / OpenGL fallback | WARP (`D3D_DRIVER_TYPE_WARP`) when hardware D3D11 fails, then `SoftwareCanvas` if Direct2D cannot be created. Linux always uses `SoftwareCanvas` | Project code |
| melatonin_blur | Direct2D `CLSID_D2D1Shadow` and `CLSID_D2D1GaussianBlur`, plus a cached alpha mask on the software canvas | Ideas only. MIT, Copyright (c) 2023 Sudara Williams. Module not compiled |
| Gin controls | Knobs, faders, buttons, meters, panels drawn in `paintFrame`. DarkKnob filmstrip, orange arc `#FF6E00`, bone pointer | Ideas only. BSD-3-Clause, Copyright (c) 2018 Roland Rabien. Module not compiled |
| `Font` / `Typeface` | DirectWrite Segoe UI and Bahnschrift on Windows. stb_truetype plus Liberation Sans, and Pirulen for the wordmark, on Linux | stb public domain. Liberation is the metric-compatible face shipped on the dev image |
| `ImageCache`, filmstrips | stb_image. WIC is the Windows decode path still available beside the CPU upload | stb public domain. WIC is Windows SDK |
| `UnitTest` / `juce::UnitTest` | Catch2 3.7.1 | BSL-1.0 |
| MIDI devices | RtMidi 6.0.0. Missing ports do not abort the app | MIT |
| CLAP | `ClapHost` returns unsupported | Stub |

## 2. Recipes

### DSP

1. Give the processor an `AudioBuffer` and a `MidiBuffer`. Do not keep a framework type in the header.
2. Copy the coefficient math, then the difference equation, and name the result in comments without the old type spelling (the source gate searches for it).
3. Transposed direct form II used here: `y = b0*x + z1; z1 = b1*x - a1*y + z2; z2 = b2*x - a2*y`. Flush state when `abs(z) < 1e-20`. `Engine::prepare` also sets SSE FTZ/DAZ (`_mm_setcsr | 0x8040`) on x86.
4. Build parameters as `Param` with the same id strings the UI and the project file use. Skew lives on the parameter (`norm()` / `setNorm()`), so a knob shows the normalized value, not a linear map of Hertz.
5. `prepare(sampleRate, maxBlock)` is where latency is fixed. The engine snapshots `latency()` when it publishes the graph. Changing a lookahead later does not move delay compensation until the graph is published again.
6. UI curves call `eqMagnitudeDb` on the message thread. They do not read filter state from the audio thread.

### UI

1. Draw into `Canvas`. The same `paintFrame` runs on Direct2D and on the software canvas.
2. `openRenderer` is the only way the window and the screenshot path create a canvas. On Windows, `Auto` creates a hardware D3D11 device with `D3D11_CREATE_DEVICE_BGRA_SUPPORT`, feature levels 11.1 and 11.0, then a WARP device, then the software canvas. `Warp` skips the hardware device and does not fall through to software. A window gets a flip-model swap chain (`DXGI_SWAP_EFFECT_FLIP_DISCARD`, two buffers, `DXGI_FORMAT_B8G8R8A8_UNORM`). A null window renders to an offscreen Direct2D bitmap. `savePng` copies that target into a CPU-read bitmap. Linux always gets the software canvas.
3. `Present` returning `DXGI_ERROR_DEVICE_REMOVED` or `DXGI_ERROR_DEVICE_RESET`, or `EndDraw` returning `D2DERR_RECREATE_TARGET`, drops the device and builds it again. Shadow and blur caches are cleared with the device.
4. Per-monitor DPI v2 is requested with `SetProcessDpiAwarenessContext` (awareness context `-4`) and applied again on `WM_DPICHANGED`.
5. The top-bar badge is the canvas backend name: `D3D11`, `WARP`, or `CPU`. It is not a generic "GPU" label.
6. Shadows: on Direct2D, draw the shape into a compatible render target and run `CLSID_D2D1Shadow`. Cache the effect by width, height, blur, and corner radius. On the software canvas, rasterize a rounded mask and box-blur it, with the same cache key. Blurs use `CLSID_D2D1GaussianBlur` or a cached box blur, keyed by the caller so a static panel is not rebuilt every frame.
7. Knobs: 100-frame DarkKnob filmstrip (`assets/DarkKnob4_Stitched.png`, 220 px frames). Frame index is the normalized, skewed parameter, not a linear frequency. The arc runs from -145° to +145° around up, in `#FF6E00`, with a bone pointer `#D1BCAC`.
8. Palette for chrome: near-black `#0A0A0A`, warm brown `#3C3732`, antique gold `#B49637`, goldenrod `#DAA520`, amber `#FF821E`. EQ band colors from the identity sheet belong on the EQ graph only.

### Parameters and state

Native devices store numbers in `DeviceState.params`. Each `Param` keeps an atomic target. The audio thread reads that target through `Device::at(index)` and one-pole smooths it. It does not look up parameters by name. VST3 changes are written into a fixed-capacity `IParameterChanges` queue and notes into a fixed-capacity `IEventList`. The callback does not call `setParamNormalized` and does not allocate those queues. A component that does not itself implement `IEditController` is connected with `getControllerClassId` and `IConnectionPoint`. `IComponent::getState` / `setState` go through a memory stream and base64 in the project file. A single-component plugin is terminated once. A separate controller is disconnected and terminated on its own. The out-of-process scan is killed after 20 seconds.

### Threading

The message thread owns the `Project` and publishes a graph. `process` increments a reader count, loads an `atomic<Graph*>`, and decrements the count when the block ends. `publish` exchanges the pointer and deletes the previous graph only after the reader count is zero. `std::atomic<std::shared_ptr>` is not used, because libstdc++ locks inside it. The test executable replaces `operator new` and counts hits while a thread-local realtime depth is positive. A 10 minute showcase run at 48 kHz and 256 frames reports zero hits. Ten thousand `setProject` calls during playback are clean under ThreadSanitizer. AddressSanitizer and UndefinedBehaviorSanitizer are clean on the same suite.

Delay lines are created in `publish` and the audio thread only advances them. A recording take is a pre-sized buffer with an atomic write index, and the clip is placed at the playhead where recording started. Live MIDI arrives through a fixed ring and is inserted into the armed instrument at a sample offset. The instrument chain of a MIDI track runs once per block, including blocks where no clip overlaps, so tails continue. An offline render constructs its own engine.

Windows audio is WASAPI through miniaudio, shared or exclusive, with the Pro Audio MMCSS usage. ASIO is a separate host in `src/audio/AsioBackend.cpp`. The SDK is not in the repository. CMake downloads ASIO SDK 2.3.4 into the build tree on Windows, or `GHOVLZ_ASIO_SDK_DIR` points at a local copy. A closed-source release that links ASIO still needs Steinberg's proprietary licence. `tools/check_no_asio_sdk.py` fails CI if the SDK files are committed.

### Message loop

`runWindow` is Win32 on Windows and X11 on Linux. The Windows loop opens the canvas through `openRenderer` and calls `begin` / `end` around `paintFrame`, so Direct2D actually draws and presents. Keys 1–8 switch views. Space toggles play. The audio callback does not paint. `--screenshot <dir>` uses the same renderer. `--force-warp` requires the backend name `WARP` and reads the PNG back from the Direct2D target. Linux CI and this VM still write the software canvas, because Direct2D is Windows-only.

## 3. What was easy, what was hard, how sound was checked

Easy:

- Project JSON instead of a value tree. The folder format is shorter than the old XML and the round-trip test is direct.
- Catch2 instead of a custom test runner. No GUI init in the test process.
- miniaudio for WASAPI and ALSA behind one callback.
- Keeping a `Canvas` so the Linux software renderer and Direct2D share one paint function.

Hard:

- The VST3 SDK's CMake builds samples and a module-info tool if you add the SDK root. This tree includes `SMTG_VST3_SDK.cmake` and creates only `base`, `pluginterfaces`, `sdk_common`, `sdk`, and `sdk_hosting`. `module_linux.cpp` / `module_win32.cpp` and `memorystream.cpp` are added by hand. `vstsinglecomponenteffect.cpp` is compiled with `PROJECT_INCLUDES_VSTEDITCONTROLLER=1` so it does not pull a second copy of the edit controller. Linux plugins also need `linuxmain.cpp` (`ModuleEntry`) or `dlopen` fails.
- `HostProcessData::prepare(component, maxBlock, ...)` allocates the channel pointers itself (`channelBufferOwner`). Passing `0` for the sample count lets the host set pointers each block. With a non-zero count the plugin processes silence.
- Hidden visibility is on for the SDK. Factory exports use `SMTG_EXPORT_SYMBOL`. A crash plugin must abort in `createInstance`, not in a static constructor, or a build step that loads the bundle will die.
- Single-component plugins must be terminated once.
- Delay compensation is applied on leaves and returns only. Groups must not add a second delay. The limiter lookahead has to be visible in `prepare` before latency is snapshotted.
- Send buffers must be cleared every block. An uninitialized return accumulates until the mix explodes.
- Looped clips have to wrap the media index. A long arrangement clip over a one-bar sample is silent after the first bar if the index is only bounds-checked.
- The old multiband dynamics defaulted to heavy offline oversampling. This port does not oversample. The sound is the un-oversampled band split. That difference is intentional until an offline factor is added.
- Dyn band filters currently share state across channels inside one block. Mono tests pass. Stereo imaging of DYN is not claimed to match.
- Linear-phase EQ rebuilds a 1024-point IR when the phase mode is on. Latency is 512 samples. The impulse test places the peak within 8 samples of 512 with every band off.

Sound checks in `tests/Tests.cpp`:

- Bell +12 dB at 1 kHz, RMS ratio between 3.2 and 4.6 after the filter settles (about +12 dB is 4×).
- Linear-phase latency is 512 and an impulse peaks there.
- Compressor and gate assertions use the tail, not the attack.
- Saturator stays finite and inside about ±1.
- Wavetable, sampler, and drum rack (note 36) produce a peak above 0.02.
- Transport at 120 BPM moves one quarter per 24 000 samples at 48 kHz. The metronome has energy at the start and near sample 24 000.
- Pre-fader send of a track at -100 dB still reaches the return. Post-fader does not.
- A limiter with 10 ms lookahead delays an impulse to sample 480 on both the dry (hard left) and wet (hard right) paths.
- Project round trip and a 24-bit offline render of the showcase.
- The test gain VST3 scans, the crash plugin is blacklisted, and a normalized gain of 0.25 turns a 0.5 sine into about a 0.25 sine.

## 4. What a JUCE-free plugin build needs

A VST3 plugin in this tree is a `MODULE` from `smtg_add_vst3plugin`, linked to `sdk`, `sdk_common`, `base`, and `pluginterfaces`, plus the platform entry (`linuxmain.cpp`, `dllmain.cpp`, or `macmain.cpp`).

Still required for a real product plugin, and not built here:

- `IPlugView` embed (HOST-07). The test gain plugin creates a native child of the parent it is given: an `HWND` (`WS_CHILD`) on Windows, an X11 window on Linux. `attached` paints the view into that child. `onSize` resizes it and paints again. On Windows, `WM_PAINT` and `WM_PRINTCLIENT` draw the same charcoal field and gold bar, so a later erase does not wipe the picture. `removed` destroys the child. The view implements `IPlugViewContentScaleSupport`: `setContentScaleFactor` scales the unscaled size and asks the host frame to `resizeView`, which calls `onSize`. The host frame also implements Linux `IRunLoop` (timers and file descriptors). `pumpEditorRunLoop()` delivers them, and the window loop calls it every frame. A ported EQ editor would paint its own controls into the same child.
- CMake install/signing, and a decision about the VST3 SDK license on anything shipped outside the company.
- CLAP, if that format is required: an entry point, parameters, and state. `ClapHost` on the host side is still a stub.
- A factory UID, bus layout, and `setBusArrangements` that match what the host activates. The gain plugin uses `SingleComponentEffect` and stereo buses.
- No Projucer exporter. The `.jucer` file is rejected by the source gate.

## 5. Modules worth sharing

**ghovlz-core** (today this is the static library of the same name):

- `core/Types.h` buffers, MIDI, parameters, decibel helpers
- `dsp/Biquad.h`, `dsp/Fft.h`, `dsp/Devices.cpp` (EQ, DYN, dynamics, filter, delay, reverb, modulation, instruments, analyzer)
- `model/Project.*` and `io/WavFile.*`
- `engine/Engine.*` transport, graph, PDC, render
- `host/PluginHost.*` VST3 scan, blacklist, process, state

**ghovlz-ui:**

- `ui/Canvas.h`, `SoftwareCanvas.cpp`, `Direct2DCanvas.cpp`
- `ui/Paint.cpp` is the DAW. A plugin would replace it and keep the canvas, the shadow cache, the filmstrip knob, and the palette.

Do not put the VST3 SDK inside `ghovlz-core` public headers if a plugin and the host would both export SDK symbols. The host links the SDK. A plugin links the SDK on its own target, which is what `GhovlzTestGain` does.

## 6. Device-chain EQ card

The EQ card on the device-chain view is a drawing: a compact curve and three band nodes, so the chain matches the warm concept. It does not process audio. The EQ DSP and editor port stays a separate piece of work (the gap plan's EQ port, not this view).

## 7. Other GHOVLZ apps

Scope is the amount of code that has to move, not a calendar estimate.

| App | What moves | Size of the port |
| --- | --- | --- |
| EQ | DSP for the eight bands, linear phase, SideCheck, and LFO is already re-expressed in `EqDevice`. The editor (graph, filmstrip faceplate, mod matrix, scopes) is the remaining work and is larger than the DSP. | Editor is the whole job. DSP can be reused. |
| Analyzer | K-weight, spectrum, scope, correlation, and short-term loudness are in `Analyzer`. The product UI is a scope shell on that snapshot. | Smaller than EQ. Mostly the view. |
| DYN | Three-band split and compressor are in `DynDevice`, without the old 16× offline oversampling. The faceplate and band display remain. | DSP reuse plus a faceplate. State the oversampling gap in the product notes. |
| Scan | File and plugin scanning. Closest to the scanner already in this tree. | Small, once the VST3 host helper is shared. |
| Fluid | A viewport application. The drawing would sit on `Canvas` or on D3D11 directly for the heavy view. The private tree was not copied. | Large UI port. Do not vendor the old viewport module. |
| Terrain, Vegetation | Engine-side tools with their own scenes. Not started, and not required for the DAW. | Large, separate from the audio graph. |

## Direct2D, shadows, and frame time

The Windows renderer is Direct2D because that backend was the one that made the other GHOVLZ apps responsive. `openRenderer` picks hardware D3D11, then WARP, then the software canvas. This VM has no D3D11 device, so Direct2D was not executed here. Windows CI compiles `Direct2DCanvas.cpp`, and `--force-warp --screenshot` reads the WARP frame back to PNG. Linux runs `SoftwareCanvas`.

Measured on the software canvas, arrangement view, 1680×980, in the screenshot pass that wrote `docs/screenshots/`:

- cold: 13.7 ms
- warm: 12.9 ms

An earlier pass, before the glyph cache, was 16.9 ms for the first 30 frames and 16.1 ms for the next 200. The Direct2D path caches `ID2D1Effect` objects the same way. This VM cannot create a D3D11 device, so those numbers are the software canvas. The Windows job is pinned to `windows-2022` because `windows-latest` is a VS 2026 image and does not provide the Visual Studio 17 2022 generator. Direct2D is compiled by that job. It had not been compiled on `windows-latest`.

Badge text on Windows is `D3D11` or `WARP`. On Linux and on the screenshot PNGs it is `CPU`. `WM_DPICHANGED` updates the Direct2D DPI before the window is resized. `dipExtent` maps 1680 physical pixels to 1680, 1120, and 840 DIP at 96, 144, and 192 DPI.
