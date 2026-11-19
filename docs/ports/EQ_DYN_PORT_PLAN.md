# EQ and DYN — native device port plan

Status: implementation plan. This PR adds documents only. No product code changes.

The native devices replace the v0.1 approximations in `src/dsp/Devices.cpp`. The target is the whole audible DSP and the product UI of each plugin, re-expressed on the DAW's JUCE-free stack. A simplified "EQ Eight" is the thing this plan retires.

Pins, read on 2026-11-19:

| Tree | SHA | Date | Subject |
| --- | --- | --- | --- |
| DAW `main` | `777e6dd` | v0.1 | JUCE-free DAW, ASIO fetched at build time |
| [GHOVLZ-EQ](https://github.com/decksounds13/GHOVLZ-EQ) `main` | `c432bdf` | 2026-08-12 | GHOVLZ! EQ |
| GHOVLZ-EQ `dyn/main` | `5cafeb6` | 2026-08-16 | GHOVLZ! DYN |
| GHOVLZ-EQ `analyzer/main` | `bde1c98` | 2026-08-12 | Analyzer suite. `SYNC_STATE.md` records absorbed EQ SHA `c432bdf` |

`docs/` may name the old framework. `tools/check_no_juce.py` rejects those names in source, plugins, tests, and CMake. New DSP lives in new translation units and talks to `ghovlz::AudioBuffer`, `Biquad`, `fftRadix2`, `Param`, and `Canvas`.

## How to use this plan

Each feature is **Port**, **Adapt**, or **Defer**.

- **Port** — same samples, same ranges, same gestures, new types.
- **Adapt** — same behavior, different host wiring (graph sidechain, project JSON, PDC republish, Direct2D).
- **Defer** — not in the port, with the reason and a ticket. DSP is not deferred. Deferred items are UI experiments and host features the plugin itself does not run.

Tickets are `PORT-*`. Size is scope, not a calendar: **S** one contained change, **M** a subsystem, **L** a full algorithm or a full view, **XL** a multi-file engine or the expanded editor.

Wireframes: [sketches/](sketches/).

---

## 1. What v0.1 already is

`docs/JUCE_PORTING_NOTES.md` is the stack map. These notes add the EQ/DYN-specific gaps. The v0.1 classes stay until the integration ticket for that device deletes them. Extending them in place will not reach parity: the algorithms differ, not just the parameter counts.

### EQ (`EqDevice`, `src/dsp/Devices.cpp`)

Eight slots, ids `onN typeN freqN gainN qN` for N = 1..8. Seven shapes (bell, shelves, notch, band-pass, HP, LP). Gain ±18 dB, Q 0.1..10, frequency skew 0.3. Plugin bank-1 gain is ±24 dB, Q 0.15..10, frequency skew 0.2, and there are 17 shapes (`Source/FilterType.h`).

`phase` selects a 1024-point impulse: the biquad chain is excited, the FFT magnitude is kept, and a linear phase ramp of 512 samples is written back. The plugin linear-phase engine is a different design: FFT 2048, FIR 1025, group delay 512, magnitude sampled from the same IIR coefficients (`Source/LinearPhaseEqEngine.h`). The shared number 512 is a coincidence of the delay, not evidence the filters match. `buildIr()` allocates on the audio thread.

`side` encodes mid/side, runs the EQ on those two channels, then optionally high-passes and low-passes the side. Plugin SideCheck is a post-EQ stage that ducks side where side exceeds mid (`Source/SideCheck.h`). The unity test with `sideAmt = 0` only checks that encode/decode is identity.

The LFO is one sine added to slot 3's frequency, advanced once per block. The plugin has three LFOs, an envelope follower, a drawable shape, and a 20-slot matrix (`Source/LfoMod.h`).

A new plugin instance does not start from the layout defaults. `EqProcessor::EqProcessor` calls `treeState.replaceState` with `FactoryDefaults::kStateXml` (`Source/FactoryDefaultsState.h`, applied at `Source/EqProcessor.cpp` around the constructor). That snapshot has `phaseMode` = 1, so the EQ stage is linear phase (group delay 512). The spectral engine's member starts as FFT (`SpectralDynamicsProcessor.h`, `method = Kind::fft`). The parameter's layout default is `SpectralMethod::defaultChoiceIndex()`, which is 1 (FFT), unless `AnalyserDefaults` has saved another index. Factory XML does not contain `SPECTRAL_METHOD_ID`, so a fresh instance keeps that default. `getLatencySamples()` returns 2048 for the FFT method whether or not a slot is armed, and the idle FFT path is a pure 2048-sample delay so PDC matches the audio (`SpectralFftEngine.cpp`). A fresh plugin therefore reports 2048 + 512 = 2560 samples with every band off. `autoGain` is on. `band1Sat` is on. Bands are off. Two comments in the plugin still call lattice the default; the initializer and `defaultChoiceIndex()` do not. The v0.1 device defaults `phase` to minimum, reports 0, and will not null against a default plugin instance.

### DYN (`DynDevice`)

One threshold, ratio, attack, release, knee, makeup, and mix shared by every band. Splits are five frequencies. The split is a single low-pass biquad per boundary with the residual passed upward, so a ratio of 1 sums flat by construction. The plugin split is `juce::dsp::LinkwitzRileyFilter` in minimum phase and a 1023-tap complementary FIR in linear phase (`Source/Dyn/DynCompressor.h`, `kFirTaps = 1023`, `kFirDelay = 511`). Per-band downward and upward thresholds, solo, clip, lookahead, oversampling, and auto-gain are absent. `docs/JUCE_PORTING_NOTES.md` says the band filters share state across channels; the current arrays are `split[2][5]` and `env[2][6]`, so that note is stale. Either way, the crossover is not the plugin's.

`dyn/main` `docs/BUILD_NOTE.md` says lookahead and linear-phase splits are not built. `DynCompressor::process` implements both. The code wins. `docs/PLAN.md` on that branch (approved 2026-08-15) is the UI spec and still lists those DSP items under "not built"; treat that section as stale and the rest of the file as the layout spec.

### Analyzer (`Analyzer`)

2048-point Hann FFT runs inside `process`, can resize, and publishes a 256-bin spectrum, 512 scope samples, correlation, a 400 ms K-weighted momentary LUFS, and peak. The plugin analyser runs on its own thread, supports FFT sizes 2048..16384, and keeps pre and post rings (`Source/Visualizer/Analyser.cpp` on `analyzer/main`). K-weight frequencies match (shelf near 1682 Hz, high-pass near 38 Hz) and the −0.691 dB offset matches. The plugin does not implement BS.1770 gating; integrated loudness is a running mean. The port keeps that approximation and says so in the meter label's tooltip source comment.

### Host gaps that block parity

- `Engine::publish` sums `Device::latency()` once per chain. Changing phase, lookahead, oversampling, or the spectral method inside `process` updates a member the delay compensator will not read until the next publish (`docs/JUCE_PORTING_NOTES.md`).
- Automation lanes apply track gain and pan only (`Engine.cpp`, `AutoApply` kinds 0 and 1).
- There is no per-device sidechain buffer. Sends are pre/post fader taps to return tracks.
- `Device::at` smooths once per call with a fixed 0.35 coefficient. The plugins smooth per sample with a time constant.
- `DeviceState.params` is `map<string, float>`. Choice parameters are stored as indices. Non-numeric state (shape curve, match curves, A/B snapshots) has to live in the existing `state` string.
- `paintFrame` draws the arrangement lane at 176 px for the track id `lead`, with up to four generic knobs (`src/ui/Paint.cpp`, `chainH`). `hitTest` covers the top bar only.
- The showcase master already chains EQ, DYN, limiter, and analyzer (`Project::makeShowcase`). The ten-minute realtime-allocation test has to stay at zero hits after the port.

---

## 2. Feature inventory

### 2.1 GHOVLZ! EQ

Identity (`JuceLibraryCode/JucePluginDefines.h` on `main`, buses in `EqProcessor.cpp`):

- Name `Decksounds - GHOVLZ! EQ`, VST3 only, version 1.0.0, manufacturer code `0x4d616e75`, plugin code `0x5a676c6b`.
- Main in/out mono or stereo, must match. Optional stereo sidechain bus, default off.
- `JucePlugin_WantsMidiInput` is 0. `processBlock` still parses note-ons when a host delivers them (LFO retrigger, per-band MIDI sidechain).
- Reported latency is `computeProcessingLatencySamples()`: spectral engine + linear-phase group delay + max saturation-oversampling latency, clamped to `kMaxBypassCompDelay` (8192). Bypass does not clear latency; the dry path is delayed to the same value.
- User data directory: `Documents/Decksounds/ParametricEq/`. Presets: `Presets/eq_presets.xml`.
- APVTS root type `PARAMETERS`. Saved property `deckStateSchema` = 2.

`FEATURES.md` describes an 8-band product and spectral processing as a filter bank. The processor is 64 bands (`EqBand::kMaxBands`) and the spectral method choice includes an FFT. The code is the spec. The sheet is useful for which analyzers the editor is expected to show.

#### Signal order

From `EqProcessor::processBlock` (normal path, not bypass / scope-pre):

```text
Match (if placed before)
  → EQ, minimum-phase IIR  or  linear-phase FIR
  → Spectral dynamics (8 armed slots)
  → Match (if placed after)
  → spectral saturation
  → SideCheck
  → structural-split solo
  → Alt-drag audition band-pass
  → auto-gain × output gain
```

Scope Pre and bypass take the dry signal, delayed by the reported latency, and still feed meters.

#### Bands and ids

Bank 1 display order is not the internal index (`Source/EqBand.h`):

| Display | Internal | Legacy id prefix |
| --- | --- | --- |
| Band 1 | 4 | `highpass` |
| Band 2 | 7 | `lowShelf` |
| Band 3 | 0 | `band1` |
| Band 4 | 1 | `band2` |
| Band 5 | 2 | `band3` |
| Band 6 | 3 | `band4` |
| Band 7 | 6 | `highShelf` |
| Band 8 | 5 | `lowpass` |

`internalFromDisplay` is `{4, 7, 0, 1, 2, 3, 6, 5}`. Default Hz: 20, 100, 300, 800, 2000, 5000, 10000, 20000.

Bands 9..64 use `eqB09`..`eqB64` plus a suffix (`extendedPrefix`). Every slot is type-agnostic after creation; the bank only picks the initial type.

Per-slot parameters (layout in `EqProcessor::createParameterLayout`, roughly `EqProcessor.cpp` 500–1771). Bank-1 names are the legacy spellings (`highpassCutoff`, `band1Frequency`, `band1Q`, `band1Gain`, `highpassOnOff`, `band1Type`, `highpassSlope`, `band1Channel`, …). Extended suffixes are `Frequency`, `Q`, `Gain`, `OnOff`, `Type`, `Slope`, `Channel`, `Dynamic`, `Sidechain`, `SidechainMidi`, `Spectral`, `DynThreshold`, `AttackMs`, `ReleaseMs`, `SpectralResHz`, `SpectralDepth`, `SpectralExpand`, `Sat`, `SatModel`, `SatPost`, `SatDriveDb`. `SplitMode` exists on bank 1 only.

Ranges that the DSP and the graph both depend on:

| Field | Range | Default in the layout | Skew |
| --- | --- | --- | --- |
| Frequency / cutoff | 20..20000 Hz | zone defaults above | 0.2 |
| Q | 0.15..10 | 0.5 HP/LP, about 0.67 bells | 0.1 HP/LP, 0.25 shelves and bells |
| Gain | −24..24 dB | 0 | linear |
| Slope | 6 / 12 / 24 / 48 / 96 dB/oct | 12 | choice |
| Channel | Stereo, Mid, Side, Left, Right | Stereo | choice |
| Dyn threshold | −120..0 dB | −24 | linear |
| Attack | 0.5..100 ms | 20 | 0.4 |
| Release | 10..1000 ms | 200 | 0.4 |
| Spectral res | 0.5..2.0 | 1.0 | 0.05 |
| Spectral depth | 0..2 | 0.5 | 0.01 |
| Sat drive | −12..12 dB | model default | 0.01 |

`createParameterLayout` registers on the order of fourteen hundred parameters once the 56 extended slots, the mod matrix, and the analyser display ids are counted. Generate the table from that function in PORT-01. Do not transcribe it by hand.

#### Filter shapes — Port

`FilterType::Choice` (`Source/FilterType.h`): bell, low shelf, high shelf, notch, band pass, high pass, low pass, tilt shelf, flat tilt, all pass, band shelf, Baxandall bass, Baxandall treble, brickwall high pass, brickwall low pass, vintage low shelf, vintage high shelf. Indices are stable; new shapes were appended.

Coefficient path:

- One biquad via the Audio EQ Cookbook forms already in `src/dsp/Biquad.h` for bell, shelves, notch, band-pass, all-pass.
- HP/LP slopes in `FilterSlope`: 6 dB/oct is first order; 12..96 dB/oct are Butterworth cascades; the user Q sits on the last stage. `FilterSlope::maxBiquadStages` is the allocation bound.
- Brickwall is eight second-order sections (`makeBrickwallStages`).
- Tilt, band shelf, Baxandall, and vintage shelves are multi-stage (`makeStages`, `FilterBandShelf.h`).
- Proportional Q (`PROPORTIONAL_Q_ID`) changes bell Q from |gain| only (`effectiveBellQ`).

v0.1 `Biquad` is the single-section kernel. Cascades are a `Biquad stages[]` sized in `prepare`.

#### Processing modes

| Feature | Behavior to match | Status |
| --- | --- | --- |
| Minimum-phase chain | Cascaded biquads per band, up to the slope's stage count, stereo. `BandChannel` encodes mid/side or left/right per band. No allocation in `processBlock` (the plugin says so near the pre-sized buffers). | Port |
| Linear phase | FFT 2048, FIR 1025, group delay 512 at any rate. Magnitude is the product of the same IIR stages. Per-band channel modes are ignored (header comment in `LinearPhaseEqEngine.h`). Per-band saturation is skipped on this branch. Dynamic gains are baked in when the IR rebuilds. Extended bands are included (`appendExtendedLinearPhaseSpecs`). This 512 is only the EQ stage. It adds to the spectral latency below. | Port. Replace `EqDevice::buildIr`. |
| Dynamic EQ | Detector is the pre-EQ dry input. Band-pass at the band frequency and Q, block RMS to dB, one-pole envelope. `effectiveGain = targetGain * amount`, `amount` saturates across 24 dB above threshold (`DynamicEq.h`). Off means the static gain. Extended bands run the same detector. | Port |
| Band sidechain | Same detector on the sidechain bus. MIDI variant: any held note forces full engage (`BandSidechain.h`). | Adapt. Needs a graph sidechain tap (PORT-04). MIDI follows the open question in §6. |
| Spectral dynamics | Eight slots armed from bank 1 (`SpectralBandSettings.h` slot count, `armSpectral`). Method `SPECTRAL_METHOD_ID`: lattice index 0 (zero latency, and a true bypass when nothing is armed) or FFT index 1. The layout default and the engine member are FFT. Lattice: log band-pass bank, res 0.5..2 Hz maps to 48..128 slices (`SpectralBinning.h`), reconstruct `wet + Σ bp*(gr−1)`. FFT: Hann, 2048, hop 512, 75% overlap-add, latency 2048 even when idle, magnitude gain reduction, phase kept (`SpectralFftEngine.h`). Idle FFT processing is a delay line of `kFftSize`, not a bypass. Detect from pre-EQ dry, optional per-slot sidechain. `spectralPerBandLattice` defaults true. `spectralPack` is Flat / LF / HF. Expand flag inverts the direction. | Port. Extended-band spectral parameters exist and are not armed. Port that limit (PORT-D4). |
| SideCheck | After EQ and spectral. HQ (default): its own log band-pass lattice, 20 Hz..20 kHz, never sharing spectral state. Eco: three bands at 500 Hz low shelf, ~1225 Hz bell (Q 0.5), 3 kHz high shelf. If side exceeds mid by > 0.15 dB, `gr = −excess * amount` clamped to 24 dB. Reconstruct side, decode. Speed Fast / Med / Slow maps to attack/release/GR smooth of 8/80/8, 40/300/28, 120/900/70 ms. HP/LP (`sideCheckHpHz` default 0, `sideCheckLpHz` default 18000) gate which slices receive GR. Amount default 0.65. Hard bypass when off and settled. | Port. `sideCheckHpSlope` / `sideCheckLpSlope` are read and are not registered; the DSP falls back to 12 dB/oct. Keep the fallback (PORT-D5). |
| Match EQ | Global lattice toward a factory or captured curve. Resolution picks 16..32 slices. Placement before or after the EQ. Amount, speed, smooth, HP/LP with slopes, freeze. User curves live in the state child `matchUserCurves`, not in APVTS. | Port |
| Structural split | Bank-1 gain bands can select Off / Transient / Sustain. A separator (`StructuralSplitEngine`) masks the EQ delta. Global solo replaces the output with dry × mask. Separation 0..100, default 50. Inactive when linear phase is on. | Port |
| Saturation | Stage 1 per band, Pre or Post, models Tape / Tube / Diode / Dual-Triode (`BandSaturation.h`). Global `satOversample`: Off / 2× / 4× / 8×. Stage 2 `spectralSat` after spectral, own model, drive 0..1 default 0.35, own oversampling choice. Oversampling is `juce::dsp::Oversampling`. Latency is the max across engines. Linear phase skips stage 1. | Port. Re-derive the half-band filters (see §3). |
| Output, scale, auto-gain | `outputGain` ±24 dB. `eqScale` 0..2, default 1, scales band gains, spectral amount, and match. `autoGain` chases pre-EQ loudness. Factory snapshot turns it on. | Port |
| LFO / shape / matrix | LFOs 1..3: shape (sine, triangle, square, saw up, saw down), rate 0.05..20 skew 0.35, `RateSync`, `SyncDiv` (17 divisions, default 1/4 so index 7 in the factory snapshot), phase 0..360, retrig Off/MIDI. Envelope: `modEnvThreshold` −60..0, attack and release same law as dynamic EQ. Slots 1..20: source, amount 0..100 default 50, polar, dest, on. Sources: Off, LFO 1..3, envelope follower, shape. Destinations: freq/gain/Q for the four peaking bands and both shelves (not HP/LP). Modulation is an offset on the smoothed value; the stored parameter stays unmodulated. Shape geometry is the `SHAPE_CURVE` value-tree child, LUT 512. | Port. Tempo sync reads the engine BPM. |
| Listen / audition | `setBandListenIndex` is UI state, not APVTS: one band forced on. Alt+drag writes atomics `auditionBandpass*` and a temporary dry band-pass. | Port |
| Bypass | Parameter `bypass`. Dry delay equals reported latency. The DAW `Device::bypass` flag stays the chain bypass and must take the same delayed path, otherwise PDC jumps. | Adapt |

#### Analyser and editor parameters — Port as display state

Registered in the same APVTS (roughly `EqProcessor.cpp` 872–1396). They do not change samples except Scope Pre, which bypasses processing. Store them in `params` so presets round-trip.

Groups: analyser block size (`BLOCK_ID` index 0..3 = 2048/4096/8192/16384, factory `blockSizeName` 8192), channel toggles, bins, range, refresh 16..200 default 33, average 1..8, max hold; spectrum curve/fill/hold/opacity/width/resolution/smooth; EQ display range (6/12/24/36 dB), path widths, fills, crosshair; FFT bar glow; oscilloscope and goniometer line/glow/quality; spectrogram FFT, speed, colour, channel, enhanced-frequency; `LOUDNESS_TARGET_ID` −23..−9 default −14; stereogram, histogram, THD; level-meter mode, fall, peak hold, clip threshold.

`menuBackgroundColor` has a listener and no parameter. Drop it.

#### UI surfaces — Port unless noted

Editor design size 1200×850, resizable 900..2400 by 500..1600 (`EqEditor.h`). Hierarchy: editor → main component (graph, meters, menu, scopes, presets) and a mod section.

Graph (`FrequencyResponseComponent`), hit radius about 10 px:

- Drag a node: X is log frequency 20 Hz..20 kHz, Y is gain inside the display range.
- Wheel on a node: Q, or cycle slope when the type is HP/LP.
- Alt+drag on empty plot: temporary audition band-pass, frequency from X, Q from Y. Takes priority over handles.
- Shift+click: toggle multi-select. Drag moves the group.
- Empty plot drag: marquee.
- Double-click a node: reset that band.
- Shift+double-click: spawn a bell at 2× the frequency.
- Right-click: context for the node, the empty plot, Learn, and Match.
- Spectral-amount handle: X frequency, Y amount, with `eqScale` inverted so the stored depth matches the drag.
- Display order of curves: static EQ, then spectral GR, then SideCheck GR.

Faceplate is eight columns for the current bank, with a bank switch for bands 9..64. Knobs are the `RotaryImageKnob*` family on `DarkKnob4_Stitched.png`.

Compact chrome on the plugin window: oscilloscope, goniometer, spectrum toggles, in/out meters. Scope mode is a quad (goniometer, spectrum, oscilloscope, spectrogram) with a pre/post tap. Extra scope modules (`Source/ScopeModules.h`): stereogram, loudness, histogram, THD, spectrogram 3D, level in/out.

Top chrome: bypass, A/B/C/D, undo, preset bar, eco, phase, settings. A/B snapshots omit `bypass`.

Learn (`Source/Learn/`) is UI-driven: it captures analyser frames, fits bands, and writes parameters through the undo manager. It is wired from the graph. Port it. It is not a separate hidden DSP.

| UI item | Status |
| --- | --- |
| Graph, faceplate, mod section, meters, scope quad, the 2D analysers, presets, A–D, eco, themes as colour tokens | Port |
| In-lane compact graph and in-lane expand | Adapt. The plugin's "compact" is a collapsed plugin window. The DAW lane is specified in §4. |
| Spec3D OpenGL heightfield, particle node graph, particle force stack, MP4 export (`SPEC3D_EXPORT_ENABLED` is 0), screen capture | Defer. PORT-D1. The 2D spectrogram still ports. |
| Full colour-ramp lab (timeline, eyedropper, path sampler) | Defer the editor. PORT-D2. Ship the default ramp tokens the meters already use. |
| Detached OS window | Defer. PORT-D3. The large panel is `View::Device` inside the one DAW window. `runWindow` is a single Win32 or X11 window. |
| AU, AAX, standalone, a VST3 `IPlugView` of this device | Defer. The device is native. Hosting the old VST3 remains `PluginHost`. |

#### State — Adapt

| Blob | Where it lives in the plugin | DAW home |
| --- | --- | --- |
| Numeric parameters | APVTS `PARAM id value` | `DeviceState.params`, plugin id strings, floats. Choices and bools are indices and 0/1. |
| Factory snapshot | `FactoryDefaults::kStateXml`, applied in the constructor | Defaults of a new `eq` device. PORT-01 emits them. |
| A/B/C/D | Properties `abSnapshotA`..`D`, `abActiveSlot` | JSON inside `DeviceState.state` |
| Shape curve | Child `SHAPE_CURVE` | JSON array in `state` |
| Match user curves | Child `matchUserCurves` | JSON in `state` |
| UI session | `uiSessionB64` or child `UiSession` (themes, scope layout, editor size, ramp banks) | JSON in `state` for the fields the DAW UI actually has. Opaque plugin blobs wait on a captured fixture (PORT-58). |
| Disk presets | `eq_presets.xml` value-tree XML | Importer in PORT-57. |
| Host chunk | `copyXmlToBinary` of that XML | Do not reimplement the binary header from memory. The reference harness writes plain XML. A chunk parser is PORT-D6, after a real fixture is checked in. |

`deckStateSchema` 2 is the version the importer accepts. Older XML still loads if the `PARAM` tags are readable; missing ids take layout defaults.

### 2.2 GHOVLZ! DYN

Identity: display name `Decksounds - GHOVLZ! DYN`, plugin code `Zgld` (`0x5a676c64`), bundle `com.Decksounds.GhovlzDyn`, VST3 only. User data `Documents/Decksounds/GhovlzDyn/`. The audio processor class is still `EqProcessor`. `processBlock` runs `dynEngine.process` and returns before the EQ chain (`EqProcessor.cpp` on `dyn/main`, the comment "compressor is the audio path"). The editor calls `setDynShellMode(true)` and builds `DynFaceplate`. The EQ parameters remain registered because the translation unit is shared. The DYN device does not grow those EQ parameters.

The sidechain bus is declared and `DynCompressor` does not read it. Port that. See §6.

`FEATURES.md` on `dyn/main` is the EQ sheet. Ignore it for this product. `docs/PLAN.md` is the layout spec.

#### Parameters (`Source/Dyn/DynParams.h`) — Port

Global:

| Id | Range | Default |
| --- | --- | --- |
| `dynBandCount` | 1..6 int | 1 (broadband, no split) |
| `dynSelected` | 0..5 | 0 |
| `dynFaceAll` | bool | false (Focus) |
| `dynLookaheadMs` | 0..20 ms, step 0.01, skew 0.4 | 0 |
| `DYN_GR_AVG_MS_ID` | 20..2000, skew 0.45 | 180 (meter only) |
| `DYN_GR_FALL_MS_ID` | 20..4000, skew 0.45 | 400 (meter only) |
| `dynAutoGainMode` | Multiband / Global | Multiband |
| `dynGlobalTime` | 10..400, skew 0.45 | 100 (percent; 100 = attack/release as written) |
| `dynGlobalAmount` | 0..200 | 100 (blend every ratio toward 1:1) |
| `dynDownAmt` | 0..200 | 100 |
| `dynUpAmt` | 0..200 | 100 |
| `dynDetect` | Peak / RMS | Peak |
| `dynOsRealtime` | Off, 2×, 4×, 8× | Off |
| `dynOsOffline` | Off, 2×, 4×, 8×, 16× | 16× (choice index 4) |

Per band `dynB1`..`dynB6`, suffix in the name (`thresholdId` → `dynB1Threshold`):

| Suffix | Range | Default |
| --- | --- | --- |
| `Threshold` | −60..0 step 0.1 | −18 |
| `UpThreshold` | −60..0 | −36 |
| `Ratio` | 0.01..100, skew centred on 1 | 4 |
| `Attack` | 0.1..200 ms, skew 0.4 | 12 |
| `Release` | 10..2000 ms, skew 0.4 | 180 |
| `Knee` | 0..24 dB | 8 |
| `Makeup` | −24..24 | 0 |
| `Mix` | 0..100 % | 100 |
| `On` | bool | true |
| `Solo` | bool | false |
| `Auto` | bool | false. Registered. `DynCompressor` never reads it. Store it so presets round-trip; DSP ignores it (PORT-D7). |
| `SplitHz` | 20..20000, skew 0.3 | 120, 400, 1200, 3500, 8000, 20000 |
| `Clip` | 0..24 dB drive | 0 |
| `ClipThr` | −60..0 | 0 |
| `ClipMode` | Soft / Hard | Soft |

`orderThresholds` keeps at least 1 dB between down and up thresholds.

The faceplate also drives `phaseMode`, `autoGain`, `outputGain`, and `bypass` from the shared processor. Those four ids exist on the DYN device too.

JUCE `setSkewForCentre(1)` on the ratio range is not a skew of 1. Convert with the same log formula the old framework uses (`log(0.5) / log((centre-start)/(end-start))`) and store that on `Param.skew`.

#### DSP — Port

`DynCompressor::process`:

- Band count 1 is broadband: no crossover.
- Count > 1, minimum phase: Linkwitz-Riley low-pass into the band below the split and high-pass above it. Match the complementary all-pass compensation the old filter uses so the bands sum flat. A single-biquad residual split will not null.
- Count > 1, linear phase: 1023-tap windowed-sinc low-pass per split, bands are differences of successive low-passes, dry is delayed by 511 samples for the mix.
- Detector: one envelope per band from the max absolute sample across channels (stereo linked). Peak uses that sample. RMS uses a one-pole on the squared value with coefficient `exp(-1/(0.005*sr))` (about 5 ms).
- Attack and release poles are `exp(-1/(max(0.001, ms*0.001)*sr))` after `scaleTimeMs` (`ms * timePct/100`).
- Gain law is `gainDbDual` / `compressAmountDb` in `DynParams.h`. Ratio > 1 compresses above the down threshold and expands below the up threshold. Ratio < 1 is upward only. Knee is the soft quadratic used there. Upward is silenced below −72 dB and fades from −60. Down and up amounts scale each direction toward 1:1 via `scaleRatio`. Global amount scales the written ratio the same way.
- Lookahead: per-band ring and a dry ring. Delay is `round(ms * 0.001 * processSr)`, capped to the prepared buffer. Gain reduction is applied to the delayed band. Reported lookahead uses the **host** rate: `round(lookMs * 0.001 * sr)`.
- Oversampling: `juce::dsp::Oversampling<float>`, half-band polyphase IIR, 2 channels. Realtime factor is `dynOsRealtime` (log2 0..3). Offline render uses `dynOsOffline` (log2 0..4). The whole split-and-dynamics chain runs at the elevated rate. `isNonRealtime()` is how the plugin picks the offline factor. The DAW equivalent is the `renderToWav` path.
- Mix is per band: `dry*(1-mix) + wet*makeup*mix`. Any solo mutes the other bands. Clip (`clipSample`) runs on the band before the dynamics: tanh soft, or hard ceiling.
- Auto-gain reads `autoGain` plus `dynAutoGainMode`. Multiband chases each band's need. Global chases output RMS against the delayed dry.
- Meters: atomics `grDb`, `upGrDb`, `clipDb`, `inDb` per band, relaxed loads. UI smooths GR with the two meter time constants.

Latency, copied from the end of `DynCompressor::process`:

```text
lookLat = round(lookaheadMs * 0.001 * hostSr)
lat     = ceil(oversamplingLatencySamples)          // 0 when OS is off
        + (linearPhase ? (kFirDelay >> osLog2) : 0) // 511 at 1×, halved per OS octave
reported = max(0, lat + lookLat)
```

The FIR delay is shifted because the FIR runs at the oversampled rate and the host sees that delay divided by the factor. Keep the shift. Do not "correct" it.

`DynCompressor::process` calls `AudioBuffer::setSize` on the audio thread when the block grows. The port preallocates `maxBlock * (1 << preparedOsLog2)` in `prepare`. Samples must still match.

Not in the plugin, so not in the port: per-band mid/side, a mute distinct from solo, crossover slope choices, an external sidechain key into the detector. `docs/PLAN.md` mentions several of these as wishes. The DSP file is the list.

#### Split learn — Port

`DynSplitLearn`: about 5 s at 20 Hz. With two or more bands and eco off, it accumulates analyser magnitudes, searches valleys at least 0.55 octaves apart, and writes `dynBnSplitHz`. Thresholds nudge from the per-band envelope min/max and `tickleDistDb`. Clip learn chases about 1.5 dB of `getClipDb`. This is a message-thread / timer task. It reads the analysis snapshot. It does not run inside `process`.

#### UI — Port the r3 faceplate, Adapt the lane

`docs/PLAN.md` (approved 2026-08-15) supersedes the r2 mid-row oscilloscope. `setDynShellMode(true)` hides Osc, Gon, Spec, the scope-mode button, and the piano. The attached r2 concept with a full-width oscilloscope under the spectrum is that older layout. The binding expanded UI is:

1. Top trim: bypass, A–D, preset bar, save, undo, eco, realtime OS, Peak/RMS, global Learn, settings, Focus/All, collapse.
2. Spectrum, the only large plot, with taller in/out meters. Drag splits (vertical lines), down threshold, up threshold, clip threshold, and makeup. Double-click empty spectrum adds a band up to 6. Wheel edits ratio. Right-click solos or deletes. Hit radius in `DynSpectrumOverlay.cpp` is 8 px; threshold hits are a few pixels larger. Copy those constants from the file when implementing.
3. Focus row, same height as the spectrum: a square transfer plot (knee, ratio, live ball from `getInputEnvelopeDb`) and the selected band's knobs plus a vertical GR rail. 0 dB GR is at the top; reduction grows downward. Global column: Time, Amount, Down, Up (126 px in the plugin).
4. All mode: the big transfer hides; each band is a card with a mini transfer, knobs, and a GR rail (`dynFaceAll`).
5. Bottom trim: help, Minimum / Linear Phase, wordmark, Lookahead knob, global auto-gain, output gain.

Knobs use the same filmstrip as EQ, untinted.

Editor bounds match EQ: 1200×850 design, same resize limits.

Presets: `EqPresetStore` writes `Documents/Decksounds/GhovlzDyn/Presets/eq_presets.xml`. Factory names include OTT and OTT Xfer. `makeOttState(bool)` ignores the bool and uses splits 88.3 Hz and 2500 Hz for both. Port that behavior and record it in the preset comment so nobody "fixes" the Ableton 220 / 3.4 kHz split without being asked.

DYN state is the same XML envelope as EQ. The device stores DYN ids plus `phaseMode`, `autoGain`, `outputGain`, `bypass`, and the analyser display ids the spectrum overlay needs (`BLOCK_ID`, `REFRESH_ID`, range). It does not store the 64-band EQ set.

### 2.3 Analyzer components (shared)

Source of truth for the math and the module list is `analyzer/main` at `bde1c98`, which matches EQ `main` on the allowlisted files except `Source/Menu/Gui/SpectrumComponent.cpp`. `dyn/main` has diverged copies of several of those files. Shared widgets are ported from `analyzer/main`. DYN-only behavior is whatever `setDynShellMode` hides, not a second analyser fork.

`SYNC_ALLOWLIST.md` denies `SpectralDynamicsProcessor`, `SpectralFftEngine`, and `SpectralMethod` on the analyzer branch. Spectral DSP is ported from EQ `main` only.

| Module | Math | Update | Status |
| --- | --- | --- | --- |
| Spectrum | Hann FFT, size from `BLOCK_ID` (2048..16384). Audio thread pushes a 16384 ring. A worker copies the latest N samples each `REFRESH_ID` ms (16..200, default 33). dB curves under a lock the UI reads; the audio thread does not take that lock. | Worker + UI timer | Port. Move the FFT off `Analyzer::process`. |
| Spectrogram 2D | Own Hann FFT, orders 11..14, hop derived from columns per second and clamped. Optional reassignment (`SpectrogramReassignment`). | UI timer, 60 Hz, FFT on that thread | Port |
| Oscilloscope | No FFT. Ring about 16 s. Columns from 1..32 beats at the engine BPM. | 30 Hz | Port |
| Goniometer | Lissajous plus correlation. Ring 0.25 s. | 20 Hz compact, 30 Hz expanded | Port |
| Stereogram | Hann 2048 plus an 8192 low-frequency blend. | 30 Hz | Port |
| Loudness | Hand-rolled K-weight (shelf near 1681.97 Hz at +4 dB, high-pass near 38.135 Hz). Momentary 0.4 s, short-term 3.0 s, integrated running mean of mean-square, offset −0.691 dB. No BS.1770 gate. The plugin takes a `CriticalSection` on the audio push. | Audio filter + UI 20 Hz | Adapt. Two biquads and a fixed ring on the audio thread; the UI integrates. No lock. |
| Histogram | Same K-weight, ~3 s EMA, true peak via cubic Hermite oversampling. | 30 Hz | Port |
| THD | Fixed 4096 Hann. Fundamental lock plus harmonics, or a multiband residual. `THD_MODE_ID`, max harmonics, lock dB. On `analyzer/main` the module is not fed from `EqProcessor` (no `pushSamples` wiring found). | 24 Hz | Port the component, wire it to the same tap as the other meters. Flag the missing plugin wiring in the test notes. |
| Level meters | Block peak, RMS, true peak, published as atomics from the processor. Modes peak / RMS / both, L/R or M/S, fall and hold. | 30 Hz | Port |
| Spectrogram 3D | Meshes the 2D history on an OpenGL timer. No FFT of its own. | 30 Hz, 60 Hz with particles | Defer. PORT-D1 |
| Legacy `Spectrogram` class | 1024 FFT, not the scope pipeline | — | Defer. Dead beside `SpectrogramComponent`. |

Eco mode skips the spectrum push and the analysis thread. Dynamic EQ, spectral, and SideCheck keep running. The DAW eco flag does the same: analysis worker idles, audio path does not.

---

## 3. DSP port

### 3.1 File layout

New code stays out of `Devices.cpp` until the integration ticket.

```text
src/dsp/eq/Params.h            generated ids, ranges, skews, factory defaults
src/dsp/eq/FilterDesign.h      17 shapes, slope cascades
src/dsp/eq/EqDevice.cpp        process order, latency, bypass delay
src/dsp/eq/LinearPhase.h
src/dsp/eq/DynamicEq.h
src/dsp/eq/SideCheck.h
src/dsp/eq/Spectral.h          lattice + FFT, 8 slots
src/dsp/eq/Match.h
src/dsp/eq/Split.h             transient / sustain
src/dsp/eq/Sat.h
src/dsp/eq/ModMatrix.h
src/dsp/dyn/Params.h
src/dsp/dyn/DynDevice.cpp
src/dsp/dyn/Crossover.h        Linkwitz-Riley + 1023-tap FIR
src/dsp/dyn/Law.h              gainDbDual, clipSample, ratio skew
src/dsp/dyn/Oversampling.h
src/dsp/analyze/Tap.h          ring, worker, snapshot
src/dsp/analyze/Loudness.h
src/dsp/analyze/Meters.h
```

`createDevice("eq"|"dyn"|"analyzer")` returns the new type. `eqMagnitudeDb` moves to the filter-design header and learns the 17 shapes and the slope cascades. The UI thread calls it. It does not read filter state.

`Biquad` stays the single-section kernel. Cascades, Linkwitz-Riley, and FIRs are the new files. `fftRadix2` stays the transform; realtime callers pass a buffer allocated in `prepare`. The allocating `std::vector` overload is for tests and for the UI thread.

Provenance comment on each new file, same tone as `Devices.cpp`: math re-expressed from the pinned SHA, the EQ repository publishes no license file.

### 3.2 Framework replacements

| Plugin facility | DAW replacement |
| --- | --- |
| `AudioBuffer<float>` | `ghovlz::AudioBuffer`, planar |
| `dsp::IIR::Filter` / `Coefficients` / `ProcessorDuplicator` | `Biquad` plus a per-channel stage array. Cookbook forms stay in `Biquad.h`. Multi-stage designers live in `FilterDesign.h`. |
| `dsp::LinkwitzRileyFilter` | `Crossover.h`. Two cascaded Butterworths and the complementary all-pass, prepared per channel. |
| `dsp::FIR::Filter` and the linear-phase delay line | Preallocated circular buffer, 1025 taps (EQ) or 1023 (DYN). |
| `dsp::FFT`, `WindowingFunction` Hann | `fftRadix2` on a prepared buffer. Hann window table prepared once. |
| `dsp::Oversampling`, half-band polyphase IIR | `Oversampling.h`. Design the half-band from the published specification (transition width and stopband the old class documents). Do not compile or paste the old source; the gate and the license both forbid it. |
| `LinearSmoothedValue` | Per-sample one-pole in the device, time constant copied from the `reset` calls in `prepareToPlay`. `Device::at`'s 0.35-per-block smoother is not used by EQ or DYN. |
| `SmoothedValue` of coefficients | Coefficients update when the smoothed Hz/dB/Q change. Rebuild into prepared arrays. |
| `AudioProcessorValueTreeState`, `AudioParameter*` | `Param` by integer index. The audio thread never looks up a name (`Device::get` already counts a realtime hit). |
| `NormalisableRange` skew and `setSkewForCentre` | `Param.skew`. Ratio uses the centre-skew conversion in §2.2. |
| Atomically published meters | `std::atomic<float>` relaxed, or the existing triple-buffer snapshot pattern in `Analyzer`. |
| `UndoManager` | A per-device undo stack of param maps, depth 32, message thread. Graph drags push one transaction on mouse-down, matching the plugin's "EQ edit" transaction. |
| `Thread` + `WaitableEvent` (analyser) | One analysis worker per device, started in `prepare`, joined in the destructor on the message thread. |
| `CriticalSection` inside loudness `pushSamples` | Lock-free ring. Dropped blocks are worse than a data race; the plugin's try-lock already drops them. |
| `AudioPlayHead` tempo | `Graph::bpm` and the time signature already on the published graph. Sync divisions read those. |
| `isNonRealtime()` | `Engine` sets a flag on the device before `renderToWav`. Live `process` sees realtime oversampling. |
| Value-tree XML | `params` float map + a JSON object stored in `DeviceState.state` (the serializer already writes `state` as a string). `type == "vst3"` keeps base64 in that string. `eq` and `dyn` store JSON. |
| `eq_presets.xml` | A small XML read of `PARAM` elements, in a tool or a one-file parser that does not pull a framework. Emit JSON the device can load. |
| Melatonin blur, Gin, OpenGL | Already replaced. Chrome blur uses the canvas shadow cache. Knobs use the filmstrip already loaded in `Paint.cpp`. |

### 3.3 Realtime rules

The audio callback may:

- read `Param::target` with relaxed atomics and advance the per-sample smoother;
- run biquads, FIRs, the spectral FFT, and the oversampler on prepared memory;
- write meter atomics and push the analysis ring;
- set a dirty flag when a latency-affecting parameter crosses a threshold.

The audio callback may not:

- allocate, including `vector` growth, `setSize`, map lookup by string, or `Device::get`;
- take a mutex;
- call `Engine::publish` (it waits on the reader count);
- rebuild an FFT plan or change a buffer length.

IR rebuilds, FIR coefficient rebuilds, and oversampling-factor changes allocate on the message thread inside `prepare`, then `publish`. The audio thread sees the new buffers only as part of the new graph. A dirty IR (linear-phase magnitude) uses the prepared FFT workspace and may run on the audio thread, matching the plugin, as long as the workspace exists.

Latency-affecting parameters: `phaseMode`, `dynLookaheadMs`, `dynOsRealtime`, `dynOsOffline` (when the render flag changes), `satOversample`, `spectralSatOversample`, `SPECTRAL_METHOD_ID`, and whether any spectral FFT slot is armed. The message thread that applies the parameter calls `Engine::publish` so PDC and the bypass delay update together. Automation of those few ids is applied on the message thread even if the lane is otherwise sample-accurate; document that in the automation ticket. Sample-accurate automation of frequency, gain, Q, threshold, and ratio stays on the audio thread via atomics.

`bypass` delays the dry input by `latency()` samples through a prepared line. Both the chain bypass flag and the plugin `bypass` parameter use it. Reported latency stays put while bypassed, matching `EqProcessor`.

Oversampling buffers are sized for the factor `prepare` saw. Switching factor republishes. Offline render calls `prepare` with the offline factor before the render loop. Default realtime OS is Off on both devices, matching the plugins. Default offline OS for DYN is 16×.

Channel count in this engine is stereo (`Node::work` is 2 channels). The plugin's mono bus is unused here until the engine grows a channel count. Process both channels. Mid/side and left/right are encoded inside the device.

### 3.4 Sidechain

`Device::process` gains an optional sidechain pointer, null when unconnected. `DeviceState` gains `sidechainTrack` (empty means none). On publish, the engine taps that track pre-fader, after its own device chain, into a prepared buffer. EQ per-band sidechain and spectral detect read it. With null, those modes fall back to the pre-EQ dry input, matching a plugin whose sidechain bus is unconnected.

DYN does not read the pointer. The field may still be stored so a later change is a DSP edit rather than a format edit.

This tap is the one structural engine change. It is PORT-04, and it is a conflict risk. See §5.

### 3.5 PDC

Leaf latency is the sum of device `latency()` values, captured in `publish`, which is what `routeOf` already compensates. Groups add their own chain once. Devices do not add a second delay.

EQ `latency()` returns `computeProcessingLatencySamples()` from the snapshotted mode, not from a member written inside `process`. DYN `latency()` returns the formula in §2.2 from the snapshotted oversampling factor, phase, and lookahead.

Linear phase EQ with all bands off is a pure delay of 512. The existing test allows ±8 samples. PORT-12 tightens that to the exact sample once the 1025-tap FIR is in.

DYN linear phase adds 511, plus lookahead, plus the shifted FIR term when oversampling is on. Minimum phase with lookahead 0 and OS off reports 0.

### 3.6 Parameters, presets, projects

Ids are the plugin ids. v0.1 shorthand is migrated on load:

| v0.1 id (display order) | Plugin id |
| --- | --- |
| `on1 type1 freq1 gain1 q1` | `highpassOnOff highpassType highpassCutoff highpassGain highpassQ` |
| `on2 …` | `lowShelf*` with `lowShelfFrequency` |
| `on3 …` | `band1*` (`band1Frequency`, …) |
| `on4 …` | `band2*` |
| `on5 …` | `band3*` |
| `on6 …` | `band4*` |
| `on7 …` | `highShelf*` |
| `on8 …` | `lowpass*` with `lowpassCutoff` |
| `phase` | `phaseMode` |
| `side`, `sideAmt`, `sideHp`, `sideLp` | `sideCheck`, `sideCheckAmount`, `sideCheckHpHz`, `sideCheckLpHz` |

Type indices 0..6 match `FilterType` for the seven shapes v0.1 knew. `lfoRate` and `lfoDepth` do not map onto the matrix. Leave them unused and surface a one-line note when a loaded project contains them. The v0.1 `side` path was an M/S encode of the whole EQ; after migration, `sideCheck` is the real SideCheck. Projects that had it engaged will change sound. That is called out in §6.

`docs/PROJECT_FORMAT.md` gains the new ids, the optional `sidechainTrack` field, and the JSON shape of `state` for `eq` and `dyn`. Format number stays 1: old files load, new keys are optional, shorthand ids are rewritten when seen. `deviceFrom` is the migration site.

`state` JSON:

```json
{
  "schema": 2,
  "ab": { "active": "A", "A": {}, "B": {}, "C": {}, "D": {} },
  "shape": [],
  "matchCurves": [],
  "ui": {}
}
```

A/B maps are param id → float and exclude `bypass`. Empty slots are legal.

Preset import reads `PARAM` tags from `eq_presets.xml` or from `FactoryDefaults::kStateXml` and writes `params`. The factory XML is one long line in the plugin header; the generator slices it. A new device's defaults are that factory set for EQ, and `DynParams::addParameters` defaults for DYN (broadband, one band, peak, OS off, offline 16×).

Knob normalization uses `Param::norm` / `setNorm` so the filmstrip frame tracks the skewed value. Saved project values stay in real units (Hz, dB, ms), which is what automation lanes already store.

---

## 4. UI port

### 4.1 Where it draws

`paintFrame` stays the one entry both canvases and `--screenshot` use. Device UI is not pasted into that function. New files:

```text
src/ui/devices/Hit.h          rects collected during paint, message thread
src/ui/devices/EqView.cpp     Compact, Expanded, Panel — one painter
src/ui/devices/DynView.cpp
src/ui/devices/Meters.cpp     spectrum, scope, goniometer, loudness, GR, transfer
src/ui/devices/Filmstrip.cpp  the knob() already in Paint.cpp, moved so device views can call it
```

`arrangement()` and `deviceChain()` call into the views with a `Rect`. The other agent's paint work collides only at those call sites.

`Hit` is a list of `{rect, id}` appended while painting, tested back-to-front on mouse-down. `Paint.h`'s `Hit` enum stays the top bar. Device ids are strings or a dense int the view owns (`eq:node:3`, `dyn:split:1`). Coordinates are whatever space `paintFrame` is already using. `Canvas.h` says device pixels; the porting notes describe a DIP mapping on `WM_DPICHANGED`. Device views do not invent a second space. Filmstrip source rects are in image pixels (220 px frames, `assets/DarkKnob4_Stitched.png`). Destination rects are in paint space, so DPI scales the knob. Hit testing uses the destination rect.

### 4.2 Lane behavior

Today the arrangement reserves `chainH = 176` and draws the `lead` track's devices. The port draws the selected track's chain. Selecting a track is existing engine state if the other agent has added it; until then the showcase `lead` track remains the fallback, and the view reads a `selectedTrackId` when one exists. That fallback is called out so the lane work does not block on track selection.

Compact (176 px). EQ tile width up to 280, matching the current tile cap. The body is the live graph: filled spectrum, sum curve, band nodes. No knob row. A chevron on the title expands. DYN tile shows band washes, split lines, threshold and makeup paths, and the chevron. A one-band DYN (the default) is a single full-width threshold with no splits. The three-band drawing in the sketch is the state after the user adds bands.

Expand in the lane. Chain height becomes 440 px. Sibling devices collapse to a 22 px name strip. The selected EQ or DYN fills the rest of the lane width. The painter is the same `EqView` / `DynView` with layout `Expanded`. Collapse returns to 176 px.

Large panel. `View::Device` (the current generic knob page) shows the same view with layout `Panel` at the 1200×850 design size, centered under the top bar, scroll or scale if the window is smaller than 900×500. Double-clicking the compact title and pressing the chevron expand in-lane. A button on the expanded header opens the panel. Escape collapses.

Both layouts write the same parameters. There is no compact-only subset of the DSP.

Sketches:

- [arrangement-lane.svg](sketches/arrangement-lane.svg)
- [eq-compact-lane.svg](sketches/eq-compact-lane.svg)
- [eq-expanded.svg](sketches/eq-expanded.svg)
- [dyn-compact-lane.svg](sketches/dyn-compact-lane.svg)
- [dyn-expanded-focus.svg](sketches/dyn-expanded-focus.svg)
- [dyn-expanded-all.svg](sketches/dyn-expanded-all.svg)

```text
COMPACT lane (176 px), selected track
+--------+---------------------------+----------+----------------------+
| Drums  | ........clips............ |          | meters               |
| Lead   | ......................... |          |                      |
+--------+---------------------------+----------+----------------------+
| EQ [live curve and nodes] | Glue | DYN [splits and thresholds]      |
+---------------------------+------+----------------------------------+

EXPANDED lane (440 px)
| Glue | DYN |  EQ Eight ............................................ |
|      |     |  graph, meters, faceplate, bottom trim                 |
+------+-----+--------------------------------------------------------+
```

### 4.3 Graph, spectrum, analyzers

The sum curve is evaluated on the UI thread from coefficients (`eqMagnitudeDb` extended to cascades). Compact uses 128 log-spaced points. Expanded and panel use 512. Recompute when a parameter changes or at 30 Hz while a modulator is moving the curve, so LFO motion shows up. Do not read biquad state from the audio thread.

Spectrum, scope, and goniometer read the device's `AnalysisTap` snapshot. The tap's audio side is a copy into a 16384 ring. The worker owns the FFT. Pre and post rings match the plugin when the view asks for both. Eco idles the worker.

Drawing is `Canvas` paths: filled polygon for the spectrum, polyline for the curve, filled band washes, stroked threshold lines, ellipses for nodes. That path runs on Direct2D, on WARP, and on the software canvas. No custom D3D shader, so screenshot mode can see the graph. Glow on the sum curve uses the existing cached blur, keyed by size, and only in the expanded view. The compact tile skips glow.

Band node colors are the theme tokens `GraphBand1`..`GraphBand8` (`Source/Menu/SharedResources.cpp` on the plugin). Wrap those eight across banks via `colourSlotFromGlobal`. Chrome palette stays the DAW's: near-black `#0A0A0A`, brown `#3C3732`, antique gold `#B49637`, goldenrod `#DAA520`, amber `#FF821E`, knob arc `#FF6E00`, pointer `#D1BCAC`.

Transfer plot and GR rail are `Canvas` primitives. The traveling ball reads `inDb`. GR smoothing uses `DYN_GR_AVG_MS_ID` and `DYN_GR_FALL_MS_ID` on the UI thread.

### 4.4 Hit testing and drags

Implemented once, used by compact and expanded. Modifiers match the plugin:

| Gesture | EQ | DYN |
| --- | --- | --- |
| Drag node / path | X → frequency (log 20..20 kHz), Y → gain in the current display range | Splits move in X. Down threshold, up threshold, clip threshold, makeup move in Y inside the band. |
| Wheel | Q, or slope when the band is HP/LP | Ratio |
| Alt+drag | Audition band-pass on the empty plot (freq X, Q Y) | — |
| Shift+click | Multi-select. Drag moves the group. | — |
| Double-click | Reset the band | Add a band if the plot is empty and count < 6 |
| Shift+double-click | Bell at 2× frequency | — |
| Right-click | Context: type, slope, channel, solo/listen, delete | Solo, delete |
| Drag on the GR rail | — | Down, up, and clip thresholds, same writes as the spectrum |

Fine drag: Shift slows by 10×, which is what the plugin's output and scale scrubbers do. Apply it to node drags as well so the lane is usable at 280 px wide.

Keyboard: Delete removes the selected extended band or disables a bank-1 band. Arrow keys nudge frequency (Shift = fine). Number keys 1..8 stay the DAW view switches; they do not steal band selection while a node is focused. Document that so the two shortcuts do not collide.

Undo: mouse-down begins a transaction, mouse-up commits. One Ctrl/Cmd+Z undoes the gesture.

### 4.5 Performance budgets

Measured baseline, software canvas, arrangement, 1680×980 (`docs/JUCE_PORTING_NOTES.md`): cold 13.7 ms, warm 12.9 ms. Direct2D was not run on that machine. Windows CI compiles it.

| Surface | Budget |
| --- | --- |
| Compact EQ graph + compact DYN graph, added to the arrangement frame | ≤ 2 ms on the software canvas at 1680×980. The warm arrangement frame stays under 16 ms. |
| Expanded editor, 1200×850, software canvas | ≤ 16 ms, so a 60 Hz D3D11 frame has room and the software canvas can sit at 30 Hz. |
| Curve eval | 128 or 512 magnitudes of cascades. No FFT on the UI thread for the EQ curve. Spectrogram FFT stays on its own timer and skips a frame rather than hitching paint. |
| Audio thread | Zero allocations under the existing realtime guard, showcase project, 48 kHz, 256 frames, 10 minutes, with the new EQ and DYN on the master. |
| CPU, one instance, 48 kHz, 256, CI Linux box, after settle | EQ minimum phase, 8 bands active: ≤ 0.15 ms/block. EQ linear phase: ≤ 0.40 ms/block. EQ with one spectral FFT slot armed: ≤ 0.80 ms/block. DYN, 6 bands, minimum phase, OS off: ≤ 0.25 ms/block. DYN, 6 bands, realtime 8×: ≤ 1.50 ms/block. A block is 5.33 ms of audio. |

These are CI ceilings so a regression is visible. They are not a mix-machine budget. Re-measure on Windows with WASAPI before calling them product numbers. If 8× misses the ceiling, the default stays Off, which is the plugin default, and the ticket records the number instead of silently dropping the factor.

DPI: `WM_DPICHANGED` already rebuilds the Direct2D target. Device layouts recompute rects from the new canvas size. No cached hit rect survives a resize. Filmstrip frames are not pre-scaled per DPI; `drawImage` scales.

---

## 5. Test plan

The JUCE gate means this repository cannot compile the plugins. Reference audio is rendered elsewhere and committed as fixtures.

### 5.1 Reference renderer

A small console target in the **plugin** repository (not this one) links the existing processor, sets parameters, and writes 24-bit stereo WAV at 48 kHz and at 44.1 kHz, block size 256. One job for `c432bdf` (EQ), one for `5cafeb6` (DYN). Output carries the SHA in the filename. Smoothing is disabled or the render includes a documented settle, and the DAW test skips the same settle. This repo stores:

```text
tests/fixtures/eq-dyn/README.md          command line, SHA, block size
tests/fixtures/eq-dyn/eq-*.wav
tests/fixtures/eq-dyn/dyn-*.wav
```

Fixtures land in PORT-70 after the renderer exists. Until then, DSP tickets use analytical checks (impulse peak index, RMS of a sine, flat-sum null of a ratio-1 crossover) so work is not blocked on the plugin CI.

Null metric: `20*log10(rms(error) / rms(reference))` over the compared region. Gate is **below −90 dB** where both implementations are deterministic. Compare the tail after settle when envelopes are involved. Align the DAW buffer by `latency()` before the subtract.

Cases, each a fixture and a Catch2 case:

| Case | Why it is deterministic |
| --- | --- |
| EQ, all bands off, phase forced minimum, spectral method forced lattice | Bit-identical dry, latency 0 |
| EQ, all bands off, phase linear, spectral method lattice | Impulse at sample 512, then null against a 512-sample delay |
| EQ, factory defaults (linear phase, spectral method FFT) | Impulse at sample 2560 (512 + 2048), null against that delay. This is the fresh-device case |
| EQ, one bell +12 dB at 1 kHz, Q 0.707, static, OS off, sat off | The existing RMS-ratio test stays, and a full-buffer null against the fixture joins it. The test writes `band1OnOff`, `band1Type`, `band1Frequency`, `band1Gain`, `band1Q` (plugin ids, display band 3). |
| EQ, HP 24 dB/oct and LP 48 dB/oct | Cascade order and Q-on-last-stage |
| EQ, each of the 17 shapes at one setting | Catches a missing stage |
| EQ, mid and side on two bands, stereo noise | Channel encode |
| EQ, dynamic EQ on a tone that crosses threshold | Tail null after attack |
| EQ, SideCheck HQ and eco, side louder than mid | GR law |
| EQ, spectral lattice and spectral FFT, one slot | Includes the 2048-sample FFT latency in the align |
| EQ, saturation, each model, OS off | Waveshaper. OS on is a second gate at −90 dB once `Oversampling.h` matches; if it misses, the ticket stays open rather than widening the gate. |
| DYN, 4 bands, ratio 1, minimum phase, OS off | Flat sum. The v0.1 test passes for the wrong reason; this one requires Linkwitz-Riley. |
| DYN, linear phase, ratio 1 | Align by 511, then null |
| DYN, dual thresholds, peak and RMS, lookahead 5 ms | Align by the reported latency |
| DYN, realtime 2× and offline 16×, ratio 4 | Separate OS gate, same −90 dB rule |
| DYN, solo, clip soft, clip hard | Small fixtures |
| Both, bypass on, linear phase | Dry, delayed, null against the delayed input. Latency value unchanged. |

Block sizes 256 and 512, sample rates 48000 and 44100, for the bell and the ratio-1 DYN case. The rest run at 48 kHz / 256.

### 5.2 Parameter, automation, save/load

- Sweep every generated id from min to max to min. No NaN in a 2048-frame buffer. Recovery to the factory snapshot nulls.
- Automate `band1Frequency` and `dynB1Threshold` with a lane across four bars. The rendered audio matches a fixture where the plugin received the same breakpoints per block. Sample-accurate intra-block ramps are compared with the per-sample smoother enabled on both sides.
- Automating `phaseMode` republishes. A test hooks publish count and expects one republish, and the impulse delay before and after matches `latency()`.
- Save and load a project with an EQ (factory snapshot plus one bell), a DYN (three bands, lookahead 2 ms), A/B slots, and a shape curve. The reloaded params and the `state` JSON compare equal. An old project that still has `on3`, `freq3`, `gain3` loads as `band1OnOff`, `band1Frequency`, `band1Gain`.
- Preset XML fixture (a slice of the factory line, plus an OTT preset exported as XML) loads and the split frequencies match 88.3 and 2500.

### 5.3 CPU and screenshots

CPU cases are the budgets in §4.5, printed by the test, failed if over the ceiling. The ten-minute `operator new` guard stays green.

Screenshots: extend `--screenshot` with the expanded EQ and DYN panel on the software canvas. Compare to a plugin grab at 96 DPI:

- Region boxes (graph, meters, faceplate, trim) within 4 px.
- After aligning the frequency axis, the sum curve within 1 px vertically at 32 probe frequencies.
- Palette samples at named chrome points match the tokens above.

Bitwise PNG equality is not the gate. DirectWrite and the software font differ from the plugin's typeface. Pirulen remains the wordmark.

### 5.4 What the current tests must become

`tests/Tests.cpp`:

- "bell at 1 kHz" keeps its RMS bounds and switches to plugin ids.
- "linear phase eq delays an impulse" sets the spectral method to lattice and expects peak index 512. A second case leaves factory defaults and expects peak index 2560 and `latency() == 2560`.
- "dyn and sidecheck null at unity" is replaced by the ratio-1 Linkwitz-Riley null and a SideCheck-off null. The M/S encode check goes away with `EqDevice::encodeSide`.

---

## 6. Work breakdown

Parallel means the ticket can land while another agent edits the DAW shell, because it only adds files. Integration tickets touch shared files and should be rebased onto current `main` immediately before merge.

Shared files, the conflict list: `src/dsp/Devices.cpp`, `src/dsp/Devices.h`, `src/dsp/Biquad.h`, `src/dsp/Fft.h`, `src/ui/Paint.cpp`, `src/ui/Paint.h`, `src/engine/Engine.cpp`, `src/engine/Engine.h`, `src/model/Project.cpp`, `src/model/Project.h`, `src/core/Types.h`, `tests/Tests.cpp`, `CMakeLists.txt`, `docs/PROJECT_FORMAT.md`.

Rule: feature work goes in the new directories in §3.1 and §4.1. A shared file changes in exactly one integration ticket per milestone, and the diff is the call site plus the deletion of the old class.

### Milestone A — schema and host hooks

| ID | Size | Depends | Parallel | What | Acceptance |
| --- | --- | --- | --- | --- | --- |
| PORT-01 | M | — | Yes | Python generator, run offline, reads `createParameterLayout` ids and `FactoryDefaultsState.h` from a checkout of the plugin repo. Writes `src/dsp/eq/Params.h`, `src/dsp/dyn/Params.h`, and a JSON dump under `docs/ports/generated/`. Checked in. The script lives in `tools/` and does not link a framework. | Header compiles. Factory `phaseMode` is 1. DYN default `dynBandCount` is 1. Every id in the header is unique. |
| PORT-02 | S | — | Yes | `src/dsp/Smooth.h`, per-sample one-pole, prepared coefficient. Unit test against a known step. | No heap use after prepare. Coefficient matches `exp(-1/(sec*sr))`. |
| PORT-03 | M | — | No | `Device::latency` is read only at publish. Message-thread helper `noteLatencyChanged(deviceId)` republishes. Bypass delay line prepared to the snapshotted latency. | Flipping a test device's latency from 0 to 512 moves an impulse on the next process after publish, and not before. `Tests.cpp` and `Engine.cpp` change here. |
| PORT-04 | M | PORT-03 | No | `sidechainTrack` on `DeviceState`, pre-fader tap, optional pointer into `process`. | A noise source on track B reaches an EQ sidechain detector on track A. Unconnected pointer is null. `Project.cpp`, `Engine.cpp`, `PROJECT_FORMAT.md`. |
| PORT-05 | M | PORT-02 | No | Automation lane `track:<trackId>:device:<deviceId>:<paramId>`. Sample-accurate for continuous params. Latency ids republish. | An automated bell sweeps and the RMS at the end of the bar matches a hand-stepped control. `Engine.cpp`. |
| PORT-06 | S | — | Yes | `fftRadix2` wrapper that takes a caller buffer. Analysis worker skeleton in `src/dsp/analyze/Tap.h`. | Realtime guard stays at zero when the tap is pushed from `process`. |

### Milestone B — EQ DSP

| ID | Size | Depends | Parallel | Acceptance |
| --- | --- | --- | --- | --- |
| PORT-10 | L | PORT-01 | Yes | All 17 shapes and five slopes. Magnitude of a bell, a 24 dB HP, and a brickwall matches a fixture or, before fixtures, matches a double-precision reference computed in the test from the cookbook. |
| PORT-11 | L | PORT-10 | Yes | 64-band minimum-phase chain, channel modes, proportional Q, `eqScale`. Eight active bells, stereo noise, finite, and a mid-only band leaves the side channel untouched. |
| PORT-12 | M | PORT-10 | Yes | Linear-phase FIR 1025 / delay 512, isolated from the spectral stage (method forced to lattice). Flat response nulls against a 512-sample delay. A +6 dB bell nulls against the fixture once PORT-70 lands; until then the magnitude of the FIR matches `eqMagnitudeDb` within 0.05 dB from 30 Hz to 18 kHz. |
| PORT-13 | M | PORT-11 | Yes | Dynamic EQ gain law and ballistics. A −20 dB tone under a −24 dB threshold stays near unity; a loud tone reaches the target gain. |
| PORT-14 | M | PORT-04, PORT-13 | No | External sidechain and the dry-input fallback. MIDI engage behind the §6 question; the ticket includes the code path and a test that feeds `MidiBuffer` directly. |
| PORT-15 | XL | PORT-06, PORT-11 | Yes | Lattice and FFT spectral, 8 slots. Lattice latency 0, and a true bypass when idle. FFT latency 2048, including a pure delay when nothing is armed. A sine into an armed resonance shows gain reduction in the published GR curve. Extended-band spectral params do not arm a ninth slot. |
| PORT-16 | L | PORT-11 | Yes | SideCheck HQ and eco, including the speed table and the HP/LP slice gate. Off and settled is a null. |
| PORT-17 | L | PORT-15 | Yes | Match placement before and after, freeze, user curve JSON. |
| PORT-18 | M | PORT-11 | Yes | Structural split inactive in linear phase. Solo transient passes a click and ducks a steady tone. |
| PORT-19 | L | PORT-11, PORT-03 | Yes | Four saturators, pre/post, stage 2, OS off/2/4/8, latency in `latency()`. OS-on null waits on PORT-70. |
| PORT-20 | L | PORT-02, PORT-11 | Yes | Three LFOs, sync to BPM, envelope, shape LUT, 20 slots. A slot aimed at band 1 gain moves the samples and leaves the stored parameter at its unmodulated value. |
| PORT-21 | S | PORT-11 | Yes | Auto-gain and output gain. Bypass delay. |
| PORT-22 | M | PORT-10..21, PORT-03 | No | `createDevice("eq")` returns the new device. Old `EqDevice` deleted. Id migration in `deviceFrom`. CMake lists the new files. Existing tests updated (§5.4). Showcase realtime test still zero hits. |

### Milestone C — DYN DSP

| ID | Size | Depends | Parallel | Acceptance |
| --- | --- | --- | --- | --- |
| PORT-30 | L | PORT-01, PORT-06 | Yes | Linkwitz-Riley minimum phase and 1023-tap linear phase. Ratio 1, four bands, null below −90 dB after 100 ms (minimum) and after the 511-sample align (linear). |
| PORT-31 | L | PORT-30, PORT-02 | Yes | `gainDbDual`, peak and RMS, global time/amount/down/up, stereo-linked detector. A known sine settles on the gain the law predicts within 0.05 dB. |
| PORT-32 | M | PORT-31, PORT-03 | No | Lookahead rings, latency formula including the `kFirDelay >> osLog2` term. Impulse alignment test at 0, 5, and 20 ms. |
| PORT-33 | L | PORT-32 | Yes | Half-band oversampler, realtime factors and offline 16×. `renderToWav` uses `dynOsOffline`. Live playback uses `dynOsRealtime`. No allocation when the factor changes without a republish; the message thread republishes. |
| PORT-34 | M | PORT-31 | Yes | Solo, soft clip, hard clip, multiband and global auto-gain. `dynB1Auto` is stored and ignored. |
| PORT-35 | M | PORT-06, PORT-31 | Yes | Split learn over a synthetic spectrum with two valleys. Writes splits at least 0.55 octaves apart. Runs off the audio thread. |
| PORT-36 | M | PORT-30..35 | No | `createDevice("dyn")` swap, CMake, showcase guard. Default device is one band, latency 0. |

### Milestone D — analyzers

| ID | Size | Depends | Parallel | Acceptance |
| --- | --- | --- | --- | --- |
| PORT-40 | L | PORT-06 | Yes | Spectrum worker, sizes 2048..16384, pre and post, eco idle. A 1 kHz sine peaks in the expected bin. Audio thread does not FFT. |
| PORT-41 | XL | PORT-40 | Yes | Loudness, histogram, true peak, goniometer correlation, oscilloscope columns, stereogram, THD, 2D spectrogram. K-weight of a −20 dBFS 1 kHz sine lands on the same LUFS the plugin formula produces (±0.1 LU), gating excluded on purpose. |
| PORT-42 | S | PORT-40 | No | `AnalyzerSnapshot` extended or replaced by the tap's snapshot. `readAnalyzer` keeps working for the existing test. `Types.h` is the shared edit. |

### Milestone E — UI

| ID | Size | Depends | Parallel | Acceptance |
| --- | --- | --- | --- | --- |
| PORT-50 | M | — | No | Hit-list type. `Paint.cpp` gains two call sites (arrangement lane, device view). Top-bar `hitTest` unchanged. Mouse-down on a test rect returns the device id. |
| PORT-51 | L | PORT-22, PORT-40, PORT-50 | Yes | Graph painter: curve, nodes, spectrum fill, 17 shapes. |
| PORT-52 | M | PORT-51 | No | Compact EQ tile in the 176 px lane. Drag moves `band1Frequency` / `band1Gain`. Wheel moves Q. Screenshot test. |
| PORT-53 | XL | PORT-51, PORT-20, PORT-41 | Yes | Expanded EQ: trim, graph, meters, bank faceplate, mod section, scope quad, A–D, presets button. Layout regions match §4 at 1200×850 within 4 px. |
| PORT-54 | M | PORT-36, PORT-50 | No | Compact DYN tile. Drag split and threshold. Double-click adds a band. |
| PORT-55 | XL | PORT-54, PORT-41 | Yes | r3 Focus and All, transfer, GR rail, global column, bottom trim. Oscilloscope strip is absent. |
| PORT-56 | S | PORT-50 | Yes | Filmstrip knob with skewed norm, value readout, Shift fine-drag. Frame index matches `Param::norm`. |
| PORT-57 | M | PORT-22, PORT-36 | Yes | Preset XML import and the OTT preset. |
| PORT-58 | L | PORT-53 | Yes | Device undo, A/B/C/D, eco, phase combo republishes via PORT-03. UI-session JSON for layout mode and display range. |
| PORT-59 | S | PORT-52, PORT-55 | No | `--screenshot` writes the two panels. Region and curve checks from §5.3. |

### Milestone F — parity fixtures

| ID | Size | Depends | Parallel | Acceptance |
| --- | --- | --- | --- | --- |
| PORT-70 | M | renderer in the plugin repo | Yes | Fixtures committed with SHA, rate, and block size in the README. Renderer is not in this repo. |
| PORT-71 | M | PORT-22, PORT-70 | Yes | EQ null cases in §5.1 under −90 dB. |
| PORT-72 | L | PORT-36, PORT-70 | Yes | DYN null cases, including OS. An OS miss fails the ticket. |
| PORT-73 | M | PORT-05, PORT-58, PORT-33 | No | Automation, save/load, CPU ceilings, ten-minute guard. |
| PORT-74 | S | PORT-59, PORT-70 | Yes | Screenshot report checked in next to the PNGs. |

### Deferred

| ID | Item | Reason |
| --- | --- | --- |
| PORT-D1 | Spec3D, particle node graph, MP4 export, screen capture | OpenGL / viewport work, export flag is off, DYN's approved layout hides the spectrogram. 2D spectrogram is PORT-41. Reopen if Jeremy wants the 3D view inside the device panel; it would be a D3D11 mesh, not a GL port. |
| PORT-D2 | Colour-ramp editor | The lab is its own UI. Meters ship on the default `GraphBand` and spectrum tokens. |
| PORT-D3 | Floating OS window | One window in `runWindow`. The panel covers the "large editor" requirement. |
| PORT-D4 | Arming spectral slots past 8 | The plugin registers the params and does not arm them. |
| PORT-D5 | `sideCheckHpSlope` / `sideCheckLpSlope` as real parameters | Read by DSP, absent from the layout. 12 dB/oct fallback stays. |
| PORT-D6 | `copyXmlToBinary` host-chunk parser | Needs a captured chunk fixture. XML import covers factory and disk presets. |
| PORT-D7 | `dynBnAuto` in the DSP | Stored, unused by `DynCompressor`. |
| PORT-D8 | Natural phase | Comment in `PhaseMode.h` reserves it. No implementation. |
| PORT-D9 | Wrapping these devices as a VST3 `IPlugView` | Out of scope. They are native devices. |

Suggested order when two people are in the tree: A (PORT-01, 02, 06 in parallel; 03 then 04 then 05 serialized), then B and C and D in parallel on the new directories, then E after PORT-22 and PORT-36 so the views have something to bind, then F. PORT-22 and PORT-36 are the merges that delete v0.1 code; do them when the null checks in those tickets pass, and rebase first.

---

## 7. Risks and questions for Jeremy

Risks the tickets already treat:

- **Sound mismatch from the v0.1 code.** The linear-phase IR, the "SideCheck" M/S encode, and the DYN residual split are different algorithms that happen to share a few numbers (512 samples, a unity null). Parity work replaces them. The old tests are updated in the same commit so CI cannot go green on the approximation.
- **Shared-file collisions.** The shell, the engine, and `Paint.cpp` are moving under another agent. New translation units are the mitigation. Integration tickets are short and rebased.
- **Oversampling null.** A re-derived half-band can miss −90 dB against the plugin's filters. PORT-33/72 stay open until it matches. The fallback is not to vendor the old source.
- **`DynCompressor` allocates today.** Copying that would fail the realtime guard. Preallocate, and still null.
- **A fresh EQ reports 2560 samples.** Factory `phaseMode` is linear (512) and the spectral method defaults to FFT (2048 of delay even with no slot armed). A device that defaults to minimum phase and ignores the idle FFT delay will miss a freshly opened plugin by that whole latency, not by a small coefficient error.
- **Latency and bypass.** Forgetting the dry delay makes bypass click the playhead. PORT-03 and PORT-21 own this. The cap is 8192 samples (`kMaxBypassCompDelay`).
- **64 bands × slope cascades × saturation OS** is the CPU outlier. The CI ceilings in §4.5 make it visible. Factory state leaves bands off.
- **Analyser on the audio thread** in v0.1 will not survive the guard once FFT sizes hit 16384. PORT-40 moves it.
- **Preset XML vs host chunks.** Users' saved plugin presets on disk are XML and import. DAW host chunks inside old `.vstpreset` files are the binary wrapper and wait on PORT-D6.
- **DYN docs contradict the code** on lookahead and linear phase (`BUILD_NOTE.md`, the "not built" section of `docs/PLAN.md`). Implement from `DynCompressor.cpp` at `5cafeb6`.
- **License.** The EQ repository has no license file. The port re-expresses math and stores provenance, which is what `Devices.cpp` already does. Coefficient tables are recomputed, not copied. Confirm with Jeremy before anyone pastes a header from that tree into this one.
- **Stereo assumption.** The engine processes two channels. A mono plugin layout is not exercised.

Questions:

1. **Lane vs panel.** The plan does both: compact graph in the 176 px lane, expand to 440 px in the lane, and a 1200×850 panel on the Device view. Is the in-lane expand required in the first UI milestone, or is compact-plus-panel enough to ship first? The painters are the same either way (PORT-52/54 vs PORT-53/55).
2. **Selected track.** The lane follows the selected track, with `lead` as the fallback while selection does not exist. Is track selection already owned by the other agent?
3. **Sidechain routing.** Pre-fader tap of another track, after that track's devices. Is that the routing you want, or a per-device sidechain picker like the plugin's bus?
4. **MIDI.** The plugin declares no MIDI input and still reacts to notes for LFO retrigger and per-band MIDI sidechain. Should an EQ on an audio track see that track's MIDI, only a MIDI track's notes, or neither?
5. **DYN sidechain.** The bus exists and the compressor ignores it. Leave it unused, or key the detector from the new tap?
6. **v0.1 `side` migration.** Loading an old project maps `side` onto real SideCheck, which is a different effect. Prefer that, or drop those four ids on load?
7. **`lfoRate` / `lfoDepth`.** No mapping onto the matrix. Confirm they can be dropped with a load note.
8. **Spectral method on a new device.** The engine member and `defaultChoiceIndex()` are FFT (index 1). FFT latency is 2048 samples whether or not a slot is armed, and the idle path delays the audio to match. Two comments and `FEATURES.md` still describe lattice as the default. Factory XML omits `SPECTRAL_METHOD_ID`. Confirm a new device follows the code, so factory state plus that default is 2560 samples (512 linear-phase + 2048 FFT), not 512.
9. **64 bands vs the 8-band store page.** The plan ports 64, faceplate banks of 8. Confirm.
10. **Spec3D and the colour-ramp lab.** Deferred in PORT-D1 and PORT-D2. Say if either is part of "the general UI" for the first ship.
11. **OTT Xfer splits.** Both OTT presets use 88.3 Hz and 2500 Hz because `makeOttState` ignores its flag. Port that, or split them into the two tunings the names imply?
12. **`dynBnAuto`.** Presets will contain it. Confirm the DSP keeps ignoring it.
13. **Offline 16×.** `renderToWav` would run DYN at 16× by default, which is what the plugin does offline. Confirm that is acceptable for the DAW bounce, given the CPU.
14. **Null gate −90 dB** with smoothing off and a settle skip. Any case you want loosened (saturation, spectral FFT) before the work starts?
15. **Reference renderer.** It has to live in the plugin repo because of `check_no_juce.py`. Who lands that console target, and is pinning `c432bdf` / `5cafeb6` still the right pair if those branches move?

---

## Appendix A — process cheat sheet

EQ latency:

```text
samples = spectralLatency          // lattice: 0. FFT: 2048, armed or idle
        + (phaseMode == linear ? 512 : 0)
        + max(bandSat OS latency, spectralSat OS latency)
samples = clamp(samples, 0, 8192)
```

Factory state is linear phase and the spectral default is FFT, so a new device is 2560 until the user changes one of those two. The idle FFT stage is a delay line, not a bypass. The idle lattice stage is a bypass.

DYN latency: see §2.2. Host-rate lookahead, oversampler latency, FIR delay shifted by the OS log2.

EQ process order: see §2.1. Bypass and Scope Pre skip it and delay the dry signal.

DYN process order:

```text
optional oversample up
  → split (none if 1 band, else LR or FIR)
  → per-band clip
  → lookahead delay, then gainDbDual
  → per-band makeup and mix, solo mask
  → optional auto-gain
  → oversample down
```

Parameter index: build a `constexpr` table from PORT-01. `process` holds `int` indexes. Adding a parameter is a generator change, not a search through `process`.

## Appendix B — v0.1 files the integration tickets edit

| File | Why |
| --- | --- |
| `src/dsp/Devices.cpp` | Delete `EqDevice`, `DynDevice`, `Analyzer` bodies. `createDevice` returns the new types. |
| `src/dsp/Devices.h` | `eqMagnitudeDb` and `readAnalyzer` declarations stay; definitions move. |
| `src/engine/Engine.cpp` | PDC republish, sidechain tap, device automation, offline flag. |
| `src/model/Project.cpp` | Shorthand id migration, `sidechainTrack`. |
| `src/ui/Paint.cpp` | Lane and device-view call sites. |
| `src/core/Types.h` | Only if `AnalyzerSnapshot` grows. Prefer the tap's own snapshot and leave this struct as a view. |
| `tests/Tests.cpp` | §5.4. |
| `CMakeLists.txt` | New sources. |
| `docs/PROJECT_FORMAT.md` | Ids, `state` JSON, `sidechainTrack`. |
| `docs/JUCE_PORTING_NOTES.md` | One paragraph pointing here, in PORT-22, so the two docs do not drift. |

`src/dsp/Biquad.h` and `src/dsp/Fft.h` stay as they are unless a numerical mismatch forces a comment or a constant change. New designs wrap them.
