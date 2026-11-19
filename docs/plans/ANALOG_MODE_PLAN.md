# Analog Mode plan

Status: implementation plan. This change adds documents only. No product code, because other work is in flight under `src/`.

Working name: **Drift**. Type id `drift`. Jeremy chooses the product name. The UI string is the display name, not the type id, so a rename does not touch projects.

This plan is written against `main` at `777e6dd` (v0.1, JUCE-free). Native devices live in `src/dsp/Devices.cpp`. Parameters are `Param` atomics read with `Device::at(index)` on the audio thread. Latency is whatever `Device::latency()` returns at `prepare`, summed in `Engine::publish`. The source gate in `tools/check_no_juce.py` rejects the old framework outside `docs/`.

Related docs:

- [TAPE_BEHAVIOR_SPEC.md](TAPE_BEHAVIOR_SPEC.md) — tape mode, clean-room, no code
- [TAPE_PROVENANCE.md](TAPE_PROVENANCE.md) — who studied what
- [sketches/](sketches/) — lane and expanded wireframes
- [../JUCE_PORTING_NOTES.md](../JUCE_PORTING_NOTES.md) — realtime, PDC, palette, test style

## 0. Core requirement

Identical audio in the two channels must be able to leave with two different characters. Mono in, moving stereo out. The difference is the shape of the nonlinearity, not a widener, a delay, or a mid/side trick.

Jeremy's rule, which is also the acceptance test:

> Feed identical L/R pink noise, and separately a sine. At correlation 1, L and R get different harmonic profiles and transfer curves that drift independently over time, and the output decorrelates measurably while the perceived loudness of each side stays matched. At correlation 0, output L equals output R bit-exactly.

A second control, **offset**, is not part of that sentence and must not be folded into it. Offset is a fixed per-side character difference that remains when motion depth is 0. Correlation does not scale it.

| `correlation` | `motion` | `offset` | Identical input. Expected output |
| --- | --- | --- | --- |
| 0 | any | 0 | L and R are the same float bits, for the whole render, including while the modulators move |
| 1 | > 0 | 0 | Harmonic profiles differ, transfer curves differ and keep moving, output correlation falls, loudness stays matched |
| 0 or 1 | 0 | > 0 | Curves differ by a constant amount. A window a second later shows the same L-versus-R harmonic deltas. Not bit-identical |
| 1 | > 0 | > 0 | Constant difference plus independent drift |

Correlation 0 does not turn offset off. The bit-exact test sets offset to 0. A build that zeroes offset inside the correlation control has failed the "separate control" requirement.

The control and the meter use opposite polarity. Do not "fix" one of them to match the other.

| | 0 | 1 |
| --- | --- | --- |
| Control `correlation` | identical character, bit-exact when offset is 0 | fully independent character |
| Meter `outputCorrelation` | uncorrelated audio | identical audio |

The meter is the Pearson coefficient already computed in `Analyzer` (`sum(L·R) / sqrt(sum(L²)·sum(R²))`). Help text on the knob: "0 identical, 1 independent."

### Why this is per-channel distortion

`EqDevice` can encode mid/side, and `Utility` has a width knob. Neither turns a mono source into two different waveshapes. A memoryless nonlinearity with `f(0) = 0`, applied to the side of a mono signal, stays at 0. Mid/side mode is offered for material that is already stereo. It is not the path that satisfies the core test. The default path is dual-mono: the same samples enter two nonlinearities whose parameters differ.

### Parameter law

Per channel `c`, at each sample:

```
θ_c = θ_base + Δ_c(offset, seed) + motion * m_c(t)
```

`θ` holds drive, bias, curve, and pre-emphasis frequency.

- `motion = 0` forces the third term to 0 by multiplication, not by bypassing the rest of the device.
- `offset = 0` forces `Δ_c = 0` exactly. Do not add a drawn random and multiply by zero in a way that can produce a negative zero on one channel only. Skip the draw.
- `correlation = 0` means `m_L(t)` and `m_R(t)` are the same float, copied, not recomputed. `sin(φ)` and `sin(φ + 0)` are not a safe pair.
- Endpoints of correlation are exact. In between, `m_L = (1−c)·shared + c·left` and `m_R = (1−c)·shared + c·right`. Always advance the shared stream and both unique streams, so flipping the knob does not rewind the shared path.

`Δ` is drawn once from the seed at `prepare` and held:

- Bias and curve: left and right are opposite signs, span set in section 3. One side bends positive, the other negative.
- Drive: opposite signs, a smaller span, so the loudness servo can keep up.
- Pre-emphasis frequency: opposite signs, up to half an octave at offset 1.

The seed also picks which side is the positive one, so two instances do not match. Inside one instance the signs never animate.

Both channels run the nonlinearity. Copying one channel's output onto the other is not an implementation of correlation 0. It would hide a channel-index bug, and it is wrong as soon as the inputs differ. The bit-exact test is two full evaluations on equal inputs.

### Acceptance tests

Numbers are the release bar. A test that uses a looser epsilon has to change this section.

**AM-CORE-BIT.** Offset 0, correlation 0, motion 1, noise off, azimuth 0, chew off, degrade off. Identical input. After `process`, every left sample is bit-identical to the right sample (`memcmp` on the floats). Cover every mode, every oversampling factor, and both the random source and the quadrature LFO. Run at least 2 seconds at 48 kHz on the random source so the held state has moved. Repeat with motion 0. Two separately prepared processors, fed the same samples and the same seed, match each other bit for bit as well.

**AM-CORE-MOVE.** Offset 0, correlation 1, motion 0.5, source random, rate 1 Hz, seed fixed, drive 12 dB, mode tube, bias 0.3, auto-gain on. Input A: 220 Hz sine, amplitude 0.25, both channels. Input B: pink noise from the test generator below, both channels the same samples.

- Discard the first 500 ms.
- Sine, Hann windows of 4096 samples every 100 ms for 2 seconds. In at least one window some harmonic H2–H8 differs by at least 1.5 dB between L and R.
- The published transfer parameters differ across channels, and the difference is not constant: the standard deviation of `bias_L − bias_R` across those windows is greater than 0.01 on the bias knob's ±1 scale.
- Pearson correlation of the output over the measured region is below 0.98 on the sine and below 0.95 on the noise.
- Per-channel RMS over that region matches within 0.5 dB.
- Noise only: some third-octave band differs by at least 1 dB between L and R.

**AM-CORE-OFFSET.** Motion 0, offset 0.6, correlation 0. The same measurement a second apart: each L-versus-R harmonic delta has moved by less than 0.2 dB. The curves are not bit-identical. RMS still matches within 0.5 dB. Repeating at correlation 1 changes nothing audible, because the motion term is zero.

**AM-CORE-ENV.** The envelope source, on a dual-mono input, with offset 0, does not pass AM-CORE-MOVE. The two envelopes are the same signal. The test exists to stop a later change from "helping" the envelope source by mixing in hidden noise. Document it as a negative test.

Pink generator for the tests: xoshiro128** (Blackman and Vigna, public domain) into a one-pole lowpass at 400 Hz, gain trimmed so the RMS is about −12 dBFS. Copy the buffer to both channels. Do not use `std::rand`.

Loudness match is per channel, to that channel's own dry short-term RMS, 300 ms time constant, after the nonlinearity. Identical dry levels therefore stay matched while the spectra diverge. The metric in the tests is RMS, which is what the servo controls. Do not satisfy the test by blending toward mid.

## 1. Product shape

Three homes were considered.

**(a) A native device, Drift.** A new `Device` beside Saturator, EQ Eight, and Glue. It owns the mode list, the motion engine, the offset, the meters, and the transfer display. Projects store it as `type: "drift"` and a `params` map. Format version stays 1. The existing device chain already draws a panel per device.

**(b) A per-track or master analog toggle.** A channel-strip switch that adds a fixed console nonlinearity, component tolerance, a noise floor, and crosstalk at the strip or at the summer. No mode browser, no animated curves. It needs a new field on `Track`, so the project format moves to 2. Every track pays the CPU.

**(c) An Analog section inside EQ, DYN, and Glue.** A shared processor those devices can call on the whole signal or on one band. The faceplates and the EQ/DYN port (`docs/ports/`, a separate plan, not in this tree yet) own the layout. Wiring it now means editing `EqDevice`, `DynDevice`, and `Dynamics` while that port is open.

**Ship (a).** Drift is the only shape that can host several modes, the motion engine, and the core test without a format change and without waiting on the port. The existing Saturator stays as it is: `tanh` plus a one-pole tilt, no oversampling, and it nulls L against R on a mono input. The test `saturator stays finite` keeps covering it. Drift does not replace that class.

**Stage (c) as a library seam, then a faceplate.** Drift's processor is a section object the device calls. EQ, DYN, and Glue call the same object later, on a ticket that is blocked until `docs/ports/` lands and names the files it owns. The seam is section 1.1. Landing the faceplates is AM-17, not part of the first device.

**Stage (b) last.** The toggle is a console-mode preset of the same section: low drive, motion 0, a small tolerance, optional hiss, optional crosstalk. Default off. It is the wrong place for the core test, because a mixer switch cannot show two moving curves. Tickets AM-18 and AM-19.

### 1.1 Where the code goes

New translation units, so this work does not sit inside `Devices.cpp` except for a factory line:

```
src/dsp/analog/Section.h        the processor Drift and, later, EQ/DYN/Glue call
src/dsp/analog/Waveshapers.h    memoryless modes and their antiderivatives
src/dsp/analog/Motion.h         modulators, seed, correlation, offset
src/dsp/analog/Oversampling.h   halfband up and down, latency query
src/dsp/analog/Tape.h           included only after AM-LIC-01
src/dsp/analog/DriftDevice.cpp  Device subclass, prepare / process / latency
```

`createDevice("drift")` gains one branch in `src/dsp/Devices.cpp`. `nativeDeviceTypes()` and `deviceDisplayName()` gain the id. `Engine::setDeviceParam` already writes `Param::target`; Drift reads targets once per block and ramps per sample. Two engine hooks are specified in section 4 because the tree does not have them yet: a transport callback for tempo sync, and a republish when oversampling changes latency.

Proposed call shape, as the seam AM-17 will reuse. Not landed in this change:

```
struct SectionConfig {
    int mode;
    float driveDb, bias, curve, preFreq, preAmt, dynamics, mix, outDb;
    float motion, rateHz, correlation, offset;
    int source;          // random, chaos, quadrature, envelope
    int stereo;          // left-right, or mid-side
    uint32_t seed;
    int osFactor;        // 1, 2, 4, 8, 16
    bool autoGain, guard, noiseOn;
    float guardFloor, noiseDb, xtalk;
};

struct SectionSnapshot {
    float biasL, biasR, driveL, driveR, curveL, curveR, preL, preR;
    float outputCorrelation;
    float harmonicsL[8], harmonicsR[8];
};
```

`Section::prepare` allocates. `Section::process` is realtime-safe. `Section::latencySamples` is the integer PDC value. `Section::publishSnapshot` writes a triple buffer the UI can read without taking a lock, the same pattern as `AnalyzerSnapshot`.

### 1.2 Modes on the device

Choice parameter `mode`: Tube, Tape, Transformer, Console, FET, Diode, Fold, Rectify.

Shared controls, in knob order so the lane can show the first four: `drive`, `motion`, `correlation`, `offset`, then `bias`, `curve`, `preFreq`, `preAmt`, `dynamics`, `mix`, `output`, `autoGain`, `os`, `source`, `rate`, `sync`, `seed`, `stereo`, `guard`, `guardFloor`, `noise`, `xtalk`, `tolerance`.

Tape adds `solver`, `speed`, `saturation`, `biasGain`, `gap`, `spacing`, `thickness`, `wow`, `flutter`, `chew`, `degrade`. Hidden unless the mode is tape.

Seed is an integer stored in the float parameter. Integers up to 2^24 are exact in float32, so the range is 0..16777215.

## 2. Saturation modes

Every mode sits in one shared chain. The mode replaces only the box marked CORE.

```
in → DC block → pre-emphasis(θ) → CORE(θ, state) → de-emphasis(θ)
   → DC block → dynamics restore → mix(dry delayed) → auto-gain → trim
   → noise → crosstalk
```

Pre-emphasis is a high shelf from `Biquad::highShelf`. Shelf gain in dB is `preAmt * driveDb * 0.25`. The corner is `preFreq`, and it is one of the modulated parameters. De-emphasis is the same shelf with the gain negated, at the same per-channel corner, so a low drive is transparent and a moving corner stays invertible. RBJ shelves invert this way. Both biquads already exist.

DC blocks are `Biquad::highPass` at 10 Hz, before the shelf and after the core, so a biased curve cannot walk off into a DC offset. Asymmetric modes produce even harmonics and a small DC; the block is required, not optional.

Dynamics restore is an envelope follower on the dry tap and one on the wet tap, per channel, with the attack/release coefficient already used by Glue (`1 − exp(−1/(τ·sr))`). The wet sample is multiplied by `(envDry / envWet) ^ amount`. Amount 0 skips it. This is the "put the dynamics back" control, in the spirit of the public Topos description of a follower undoing saturation's compression. It is not a compressor and it does not look ahead.

Auto-gain is slower, 300 ms, and servos each wet RMS to that channel's dry RMS. It runs after dynamics restore. Output trim is a shared dB gain after that.

Dry/wet mix uses a delay of `latency()` samples on the dry path, allocated in `prepare`, so the blend does not comb. Reported latency stays the oversampler's delay. The dry delay is internal.

CPU ceilings in section 2.9 are one stereo instance, 48 kHz, 64-sample blocks, Release, median of five 10-second runs, as a fraction of one core. They are budgets, not measurements. The test prints the number and fails above the ceiling.

### 2.1 Tube

Asymmetric, bias-dependent even harmonics. A biased triode's load line is one-sided; the musical result is a stronger second harmonic as bias leaves zero. References for that fact, and for the quality tier: K. Dempwolf, M. Holters, U. Zölzer, "A Triode Model for Guitar Amplifier Simulation with Individual Parameter Fitting," AES 131, 2011; J. Pakarinen and M. Karjalainen, "Enhanced Wave Digital Triode Model for Real-Time Tube Amplifier Emulation," IEEE TASLP, 2010.

Default tier, memoryless, ADAA-1. Two branches, crossfaded by the curve knob. Both are zero at the origin and both have closed antiderivatives, so ADAA-1 applies to each branch and the crossfade is linear.

Branch at curve 0, a biased differential-pair clip. `g` is the linear drive, `b` is bias, `|b| < 0.9`:

```
f(x) = tanh(g x + b) − tanh(b)
F(x) = (1/g) · ln(cosh(g x + b)) − x · tanh(b)     for g ≠ 0
```

At `b = 0` the map is odd and the second harmonic sits at the noise floor. At `b ≠ 0` the second harmonic rises.

Branch at curve 1, a two-sided exponential clip. Asymmetry is a pair of gains, `g+ = g (1 + b)` and `g− = g (1 − b)`:

```
x ≥ 0:   f(x) = 1 − exp(−g+ x)
         F(x) = x + exp(−g+ x) / g+

x < 0:   f(x) = −1 + exp(g− x)
         F(x) = −x + exp(g− x) / g− + (1/g+ − 1/g−)
```

The added constant makes `F` continuous at 0 when `g+ ≠ g−`. ADAA subtracts neighboring `F` values, including across a zero crossing, so a jump in `F` would be a click. The curve knob is the linear mix of the two branches. Mix their outputs and, for ADAA, mix their antiderivatives the same way. Do not ADAA a crossfade of already-shaped samples.

Quality tier, not the default: one common-cathode stage as a wave digital filter from Pakarinen and Karjalainen 2010, one Newton step per sample, stateful. No ADAA. 4× oversampling. Ship it only after the default tier meets the harmonic tests. Budget separately.

CPU, default tier, 2×: ceiling 1.0%.

### 2.2 Tape

Memoryful. Jiles-Atherton hysteresis, loss filter, bias, wow and flutter, optional chew and degrade. The whole behavior is [TAPE_BEHAVIOR_SPEC.md](TAPE_BEHAVIOR_SPEC.md). ADAA does not apply. Oversampling does.

Default live preset: RK2, 8×. Quality preset: RK4, 16×, which is the paper's rate. NR4 and NR8 are opt-in. No tape code is written until AM-LIC-01 is answered in the provenance file.

CPU: RK2 at 8× ceiling 8%. RK4 at 16× ceiling 15%, and that preset is opt-in so a full mixer of them is a user choice.

### 2.3 Transformer

Low-frequency saturation and core hysteresis. A transformer distorts more as frequency falls, because flux is the integral of voltage and the core runs out of magnetization at high flux. Holters and Zölzer, DAFx-16, is the JA model for inductors and transformers and is the quality tier. It can share the integrator with tape and must not share the ferric constants. Electrical-steel constants stay in the transformer file, taken from that paper's examples, not from the tape plugin.

Default tier, a saturating inductance, integrated with Heun (the same RK2 step as the tape spec):

```
L(I) = L0 / (1 + (I / Isat)²)
dI/dt = (V − R I) / L(I)
```

`V` is the input, `I` is the state and the output. `Isat` tracks the saturation curve knob. `L0` is set so the small-signal corner `R/(2πL0)` sits near 40 Hz, which is why a 50 Hz sine distorts before a 1 kHz sine does. `R` is chosen for unity small-signal gain. This is a lumped musical model of Faraday's law with a saturating `L`, not a copy of a device.

CPU, RK2, 4×: ceiling 4%.

### 2.4 Console / op-amp

Soft clip plus slew limiting. The long-tailed pair at the front of an op-amp is a `tanh`. Slewing is a limit on `dV/dt`. Yeh's thesis (Stanford, 2009), cited throughout the DAFx circuit literature, treats slew as part of real analog stages.

```
target = tanh(g x)
step   = clamp(target − y, −slew·T, +slew·T)
y     += step
```

`slew` scales with the curve knob, from a gentle limit to effectively off (the clamp wider than any musical step). Stateful, so ADAA wraps the `tanh` only, and the slew state runs at the oversampled rate. 4×.

CPU: ceiling 1.5%.

This mode is also the one the future track toggle uses.

### 2.5 Transistor / FET

Memoryless, ADAA-1. The Shockley equation for a FET in its quadratic region, biased so the knob can unbalance it. Let `u = g x + b`, `q(u) = u − curve · u²`, and `q_c` that quadratic clipped to [−1, 1].

```
f(x) = q_c(u) − q_c(b)
F(x) = Q(g x + b) / g − Q(b) / g − x · q_c(b)
```

`Q` is the antiderivative of `q_c` with respect to `u`, so `Q'(u) = q_c(u)`. Dividing by `g` accounts for `du/dx`. On the unclipped piece, `Q(u) = u²/2 − curve · u³/3`. Past either clip, `q_c` is constant and `Q` continues as a straight line. `f(0) = 0`. At `b = 0` and a small curve the map is nearly odd. Bias brings up the second harmonic, which is the point of the mode next to the tube. A BJT differential pair is the tube's `tanh`. The choice is named FET.

CPU, 2×: ceiling 1.0%.

### 2.6 Diode clipper

Default tier, memoryless antiparallel pair, ADAA-1. The Shockley diode into a resistor is a smooth odd clip. The first version uses an algebraic stand-in with a closed antiderivative, rather than a Lambert-W solve:

```
f(x) = x / sqrt(1 + (g x)²)
F(x) = sqrt(1 + (g x)²) / g²          for g ≠ 0
```

`F` has a constant at the origin. ADAA uses differences of `F`, so the constant drops out. The map is odd: at bias 0 the second harmonic sits below the third. Bias runs the tube's biased `tanh` first, with a small `b`, and this clip after it, which is how the mode leans into even harmonics. The THD test uses bias 0 and expects H3 above H2. If the cascade makes a single `F` impractical, ADAA the diode map and oversample the bias stage with it. Do not drop the bias path: the THD row depends on it.

Quality tier: the two-capacitor diode clipper as a state-space or wave-digital circuit from D. T. Yeh and J. O. Smith, "Simulating Guitar Distortion Circuits Using Wave Digital and Nonlinear State-Space Formulations," DAFx-08. Stateful, 4×, no ADAA. After the default tier.

CPU, default, 2×: ceiling 1.0%.

### 2.7 Fold and rectify

Two choice values, one implementation style.

Fold, a triangle fold of the overdriven signal. Piecewise linear, so the antiderivative is piecewise quadratic and ADAA-1 applies inside each piece. On a piece where the map is `f(x) = a + b x`, `F(x) = a x + b x²/2`. The fold count follows the curve knob from a single fold to several. Esqueda, Bilbao, and Välimäki's bandlimited-rectifier work is the fallback if ADAA on the pieces is not enough at 4×; prefer ADAA plus 4× first.

Rectify: half-wave `max(x, 0)` with the DC block after it, and full-wave `|x|` selected by the curve knob past its midpoint. Antiderivatives `x²/2` on the live side and 0 on the dead side, or `x|x|/2` for full-wave. Strong H2 is the test.

CPU, 4×: ceiling 1.2%.

### 2.8 What Drift deliberately does not copy

Rawton Forge Deviation and Forever 89 Topos are the listening references for the feature, not the code.

From the public Deviation notes (Isotonik Studios product page and user guide): an asymmetric waveshaper, a per-channel offset of that asymmetry, a quadrature LFO, envelope-linked DC compensation, a low band that is the only processed band, ADAA on a soft clipper, RMS auto-gain. Drift takes the ideas "asymmetry per channel" and "quadrature motion" and "auto-gain." The transfer function in section 2.1 is a biased `tanh`, not their homographic map. Drift processes the full band and uses a shelf instead of their crossover. Noise injection is the degrade/noise control, not a ring-modulated sidechain.

From the public Topos notes (Forever 89 manual page "Mix and Flux," and Polarity's 2025-03-28 write-up): Flux moves amp and speaker parameters, chaos blends toward random, a stereo width applies the motion differently to each channel, a follower restores dynamics, and a harmonics view shows even and odd content. Drift's motion source list, the correlation control, the dynamics-restore amount, and the harmonics view cover that behavior with the modulators in section 3. Drift has no amp/speaker cabinet model.

ChowTape is the tape listening reference and is clean-roomed. Section 9.

### 2.9 CPU table

| Mode | Oversampling | Solver | Ceiling |
| --- | --- | --- | --- |
| Tube, FET, diode | 2× | ADAA-1 | 1.0% |
| Fold, rectify | 4× | ADAA-1 | 1.2% |
| Console | 4× | ADAA on the tanh, then slew | 1.5% |
| Transformer | 4× | RK2 | 4% |
| Tape live | 8× | RK2 | 8% |
| Tape quality | 16× | RK4 | 15%, opt-in |
| Track toggle, console, tolerance on | 2× | same as console | 0.5% |

A miss lowers the default oversampling or the solver. It does not delete the test. Revise the ceiling in this table with the measured number in the same commit.

## 3. Stereo motion engine

Four sources. All of them honor section 0.

**Smoothed random.** Sample-and-hold of the seeded generator at the rate control, then a one-pole whose cutoff is twice the hold rate, so the steps slew. At correlation 0 the held sample is computed once and copied. At correlation 1 the left and right holds are independent streams from the same seed (stream id mixed into the seed with a fixed constant, `0x9E3779B9`, so they never match by accident). This is the source AM-CORE-MOVE uses.

**Chaos.** Lorenz, integrated with Heun at one step per sample, time scaled by the rate knob:

```
dx/dt = σ (y − x)
dy/dt = x (ρ − z) − y
dz/dt = x y − β z
```

`σ = 10`, `ρ = 28`, `β = 8/3`. Use `x` after a DC block and a fixed normalization as the modulator. A second attractor, started from a seed offset, is the independent stream. Correlation 0 uses one attractor, copied. A logistic map `x ← r x (1−x)` at `r = 3.9`, stepped at the hold rate and then slewed, is an alternate inside the same source, selected by the curve knob of the motion section only if it fits on the expanded view; Lorenz is the default. Do not run the logistic map at audio rate.

**Quadrature LFO.** `m_L = sin(φ)`, `m_R = sin(φ + correlation · π/2)`, except that correlation 0 copies `sin(φ)` rather than evaluating the shifted angle. Correlation 1 is 90 degrees, which is uncorrelated for a sine and is the continuous motion Deviation describes. It is deterministic, so "independently" in the core test is aimed at the random source. The LFO still has to pass the bit-exact test at correlation 0 and a weaker decorrelation check at correlation 1 (output Pearson below 0.98 on the sine). Phase advances from the rate knob, or from tempo.

**Envelope.** A follower on the channel input, same coefficient style as Glue. Correlation 0 uses one follower on the mid. Correlation 1 uses two followers. On dual-mono audio those followers match, so this source cannot pass AM-CORE-MOVE. That is correct. It is there for material whose channels already differ, and for dynamics-linked drive.

Rate is 0.01–8 Hz, skewed like the EQ LFO. Sync uses the transport hook in section 4 and a division choice (1 bar, 1/2, 1/4, 1/8, dotted 1/8). The device does not read `Project` itself.

Depth is the `motion` knob, 0–1, and it is the scale on `m_c` in section 0.

### Mid/side

`stereo` choice: L/R or M/S. M/S encodes like `EqDevice::encodeSide` (mid on the left slot, side on the right), runs the two cores, decodes. Correlation and offset then refer to mid versus side, not left versus right. The core tests run in L/R. Switching to M/S on a mono source does not satisfy AM-CORE-MOVE, and a comment in the processor should say why.

### Mono guard

A limiter on the motion, not a matrix. If `guard` is on and the block's output Pearson stays under `guardFloor` (default 0.7) for 200 ms, pull `motion` and `offset` down by a slow gain until the meter recovers. The user's knobs do not move. The guard defaults **off** on Drift, because the device is supposed to decorrelate, and **on** for the future track toggle. AM-CORE-MOVE fails if the guard is left on; the test turns it off. A separate test turns it on, drives correlation to 1, and expects the measured Pearson to climb back above the floor.

### Seed

`prepare` resets the generator to `seed`. `process` is the only thing that advances it. The UI preview uses a different generator. Offline render constructs its own engine, which calls `prepare` and then `process` in order, so a fixed seed repeats bit for bit. `std::rand`, `random_device`, and the clock are not used.

### Visualizer

The audio thread stores, in a preallocated triple buffer: the current `θ` for each channel, the output Pearson of the last block, and 1024 decimated samples per channel. The message thread reads the published slot, draws the two transfer curves by sampling the mode's memoryless `f` at that `θ` (hysteretic modes draw the most recent `H`/`M` pair as a short Lissajous, not a fake static curve), and runs `fftRadix2` on the 1024 samples for the harmonic bars. No filter state is read from the audio thread. Curve sampling of a memoryless `f` on the message thread is the same idea as `eqMagnitudeDb`.

## 4. Aliasing, quality, realtime

### Oversampling

Factors 1, 2, 4, 8, 16. Factor 1 is legal only for the ADAA modes and for tests. Tape refuses to run the bias oscillator unless the internal Nyquist is at least twice 55 kHz; the spec's fallback widens `k` instead.

The live oversampler is a cascade of linear-phase FIR halfbands, cutoff at a quarter of the current rate, length 33. Group delay of that filter is 16 samples at the rate it runs at. Halfband zeros keep the arithmetic down. Upsampling and downsampling each pay that delay. A stage that runs at `2^k` times the base rate contributes `16 / 2^k` base-rate samples, and the matching decimator contributes the same amount again.

| Factor | Up delay | Down delay | `latency()` |
| --- | --- | --- | --- |
| 2× | 8 | 8 | 16 |
| 4× | 12 | 12 | 24 |
| 8× | 14 | 14 | 28 |
| 16× | 15 | 15 | 30 |

`latency()` returns that integer. `Engine::publish` already sums `latency()` into `chainLatency` and the leaf PDC delay. Changing the factor on a live device does not update PDC today: `setDeviceParam` writes the atomic and does not publish. AM-02b publishes when the `os` parameter changes, and only then. Other parameters must not republish, or the graph will churn under automation.

A later offline render may use a longer FIR. It must report its own latency the same way. Do not change latency inside a render.

Polyphase IIR halfbands (two-path allpass) are a CPU optimization only after the FIR form meets the alias test. IIR group delay is not an integer constant, and PDC on this engine is one integer. If an IIR form is added, it is a separate choice and its reported latency is the delay at 1 kHz, rounded, with the phase error written down. The first ship is the FIR.

### ADAA

Memoryless modes use first-order antiderivative antialiasing.

J. D. Parker, V. Zavalishin, E. Le Bivic, "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution," DAFx-16.

S. Bilbao, F. Esqueda, J. D. Parker, V. Välimäki, "Antiderivative Antialiasing for Memoryless Nonlinearities," IEEE Signal Processing Letters, 2017. First-order ADAA in that paper is the Parker form. Second order is a follow-up ticket if the alias test misses at 2×.

```
y[n] = (F(x[n]) − F(x[n−1])) / (x[n] − x[n−1])     when |x[n] − x[n−1]| > 10^−5
y[n] = f(x[n])                                       otherwise
```

`F' = f`. Each mode in section 2 states `F`. The fallback threshold is the practical one discussed in the public ADAA notes at https://ccrma.stanford.edu/~jatin/Notebooks/adaa.html . Those notes are about the Parker method. They are not the tape plugin.

ADAA is not applied to tape, the transformer ODE, console slew, or any WDF tier. Those use oversampling only.

### DC, denormals, smoothing

DC blocks as in section 2. Biquad state flushes at `1e−20`, which `Biquad::tick` already does. The engine sets FTZ/DAZ on x86 in `Engine::process`. Analog code still flushes its own extra state (`M`, FIR delay, Lorenz) because a unit test calls `process` without the engine.

Smoothing: read each `target` once per block, then ramp to it over the block with a 10 ms one-pole. Do not use `Device::at()` once and hold for the whole block on drive, bias, curve, or pre-emphasis. `at()` is the right reader for switches and for the oversampling factor. Calling `get()` by name on the audio thread trips `RtGuard`.

### Realtime contract

`prepare` allocates every buffer for `maxBlock` at 16×, the FIR, the dry delay, the wow delay, and the snapshot slots. `process` does not allocate, lock, or take a mutex. Growing a `std::vector` in `process` is a test failure under the existing `operator new` hook in `tests/Tests.cpp`.

No `juce::` names, no JUCE module, no FetchContent of AnalogTapeModel. The gate already fails the build on the framework's type names. A loss-filter rebuild writes coefficients into a spare buffer allocated in `prepare`, then publishes the pointer. The audio thread does not grow a vector to do it. Oversampling coefficients themselves stay fixed for the life of a `prepare`.

## 5. Track-mode realism

This section is the behavior of the staged toggle (b), and the `tolerance`, `noise`, and `xtalk` knobs that already exist on the device so the behavior can be tested before the mixer UI exists.

**Tolerance.** At `prepare`, draw fixed gain, bias, and cutoff offsets from the seed. Gaussian via Box-Muller, then frozen. Spans at tolerance 1: drive ±1%, bias ±0.02 on the ±1 scale, pre-emphasis frequency ±2%. These are not the musical offset. Musical offset is bipolar and opposite across the channels by construction. Tolerance is a small component scatter and can share a sign. Persist by the seed, not by a second block of state, so format 1 projects that contain a Drift device already round-trip. The toggle stores its seed on the track in format 2.

**Noise floor.** Default off on the device (−80 dB knob floor means off). The toggle's default, when the product turns it on, is about −72 dBFS, high-passed at 2 kHz. Correlation 0 copies one noise sample to both channels. The bit-exact test leaves noise off.

**Crosstalk.** `yL += xtalk · yR` delayed by one sample, and the symmetric path, with `xtalk` from 0 down to a default of 0 on the device and about −50 dB on the toggle. Identical channels stay identical, so correlation 0 with offset 0 still bit-matches. Allocate the one-sample state in `prepare`.

Tolerance, noise, and crosstalk do not run inside the oversampler. They are the last stage, at the base rate.

## 6. UI

Palette and drawing already in `paintFrame`: field `#0A0A0A`, panel `#161311`, warm line `#3C3732`, antique gold `#B49637`, goldenrod `#DAA520`, amber `#FF821E`, knob arc `#FF6E00`, bone pointer `#D1BCAC`. Knobs are the DarkKnob filmstrip. Drift uses that chrome. It does not introduce a new faceplate style.

Left curve and left harmonic bars are gold. Right curve and right bars are amber. That split is the whole point of the panel, and it stays readable on the charcoal field.

Wireframes: [sketches/drift-wireframes.txt](sketches/drift-wireframes.txt), [sketches/drift-lane.svg](sketches/drift-lane.svg), [sketches/drift-expanded.svg](sketches/drift-expanded.svg).

**Lane.** `deviceChain()` in `src/ui/Paint.cpp` already frames each device and dumps the first six parameters into knobs. For `type == "drift"`, replace that body with the lane: transfer pair, four knobs (drive, motion, correlation, offset), a thin output-correlation bar, eight harmonic ticks per channel, and the L/R versus M/S and OS readouts. Other devices keep the generic knobs.

**Expanded.** The same panel, wider, when that device is the focused one. Add bias, curve, pre, dynamics, mix, output, the motion row (source, rate, depth, correlation, offset, sync), the guard, and the seed. Tape mode reveals its row under the motion row. The lane does not grow to fit tape.

**Track toggle,** later. One gold pill on the mixer strip, labeled ANALOG, plus a trim. It does not draw curves.

The message thread reads `SectionSnapshot` only. A 30 Hz paint is enough for the curves to feel alive. Audio does not wait on paint.

`--screenshot` should include a device view with a Drift on the lead chain once the painter exists, so Linux CI keeps a PNG. That is AM-16, and it edits the showcase project. Land it in its own commit so the screenshot diff is obvious.

## 7. Tests

Catch2, next to the cases in `tests/Tests.cpp`. The processor can be tested without the engine by constructing the section directly, which keeps AM-10 from depending on the factory line.

| Test | Bar |
| --- | --- |
| AM-CORE-BIT, AM-CORE-MOVE, AM-CORE-OFFSET, AM-CORE-ENV | Section 0 |
| THD, tube | 1 kHz at −12 dBFS, bias 0.4, drive 12 dB. H2 at least 6 dB above H3. Bias 0: H2 at least 20 dB below H3 |
| THD, diode | Bias 0, drive 18 dB. H3 above H2. With bias 0.4, H2 rises by at least 6 dB |
| THD, fold | Energy past H5 is higher than the tube case at the same drive |
| THD, rectify | H2 is the strongest harmonic |
| THD, transformer | 50 Hz distorts more than 1 kHz at the same drive: H3 at least 6 dB hotter at 50 Hz |
| THD, console | Output slope between adjacent samples does not exceed the slew setting, and a slow sine still shows the tanh's odd series |
| Tape history, speed, dead zone | [TAPE_BEHAVIOR_SPEC.md](TAPE_BEHAVIOR_SPEC.md) section 11 |
| Alias sweep | Sine, amplitude 0.5, 1 second, discard 100 ms, FFT 16384. Alias energy is every bin that is not an integer harmonic of the input, summed, over the fundamental. Sweep 20 frequencies from 100 Hz to 0.45·fs, plus a tone at `fs·0.26` so the second harmonic sits just over Nyquist before oversampling. Waveshapers at their default OS: alias below −60 dB relative to the fundamental. Tape RK2 at 8×: below −48 dB. Every frequency must pass |
| Dry transparency | Motion 0, offset 0, drive at its minimum, mix 1, noise off, bias 0. Memoryless modes: residual under −90 dB. Tape: residual under −70 dB with the bias oscillator off, and the hiss test is separate |
| Determinism | Two renders, same seed, same input, `memcmp` equal. A third render with the seed plus one differs |
| Guard | Section 3 |
| CPU | Section 2.9 |
| Realtime | `RtScope` around 10 seconds of Drift at 64 frames. Zero `rtGuardHits`. The existing showcase test stays green |
| TSan | The suite, including a thread that calls `setDeviceParam` while another calls `process`. No new races. Snapshot publish is a release store, the reader an acquire load |
| Project round trip | A Drift's drive, correlation, offset, and seed survive `saveToFolder` / `loadFromFolder` |

Alias and THD tests need a small in-test FFT. `fftRadix2` is already in the tree. Use it.

## 8. Work breakdown

Sizes are scope, not a schedule. S is one contained change. M is a subsystem with its tests. L is a multi-part feature.

Anything that only adds files under `src/dsp/analog/` and tests can run beside the current DAW work and beside the EQ/DYN port. Anything that edits `EqDevice`, `DynDevice`, or `Dynamics` waits for `docs/ports/`. `Paint.cpp`, `Devices.cpp`, and `Engine.cpp` are shared: the tickets below say so, and the edits are meant to be a few lines.

### Milestone A — library, no faceplate

| ID | Size | Depends on | Parallel with the port | Acceptance |
| --- | --- | --- | --- | --- |
| AM-01 | M | — | Yes | `Section` prepares and processes silence to silence. No allocation under `RtScope`. Headers do not include a plugin SDK type |
| AM-02 | M | AM-01 | Yes | Halfband cascade hits the latencies in section 4. A swept sine at 4× meets the alias bar with the core bypassed (linear path). Reported latency matches a delayed impulse |
| AM-02b | S | AM-02 | Coordinate. One branch in `setDeviceParam` | Changing `os` republishes the graph. Changing `drive` does not. PDC test: a Drift at 4× delays an impulse by 24 samples on a dry track beside it, the same shape as the limiter lookahead test |
| AM-03 | M | AM-01 | Yes | ADAA tanh of a 0.45·fs sine beats naive tanh by 20 dB of alias energy at 1×. DC block kills a 0 Hz offset. Denormal state returns to 0 |
| AM-04 | L | AM-01 | Yes | AM-CORE-BIT and AM-CORE-OFFSET pass on a stand-in core `f(x) = tanh(g(θ) x + b(θ))`. All four sources run. Seed repeats |
| AM-04b | S | AM-04 | Coordinate. A defaulted `Device::setTransport` and one call in `Engine::process` | Synced rate at 120 BPM, 1/4 note, completes a cycle in 24 000 samples at 48 kHz. Devices that ignore the hook do not change |
| AM-05 | M | AM-04 | Yes | Dual-mono sine, correlation 1, RMS within 0.5 dB after 500 ms. Dynamics amount 1 brings the crest factor of a clipped drum-like burst back within 1 dB of the dry crest |
| AM-06 | L | AM-03, AM-05 | Yes | Tube, FET, diode, fold, rectify meet their THD rows and the alias bar |
| AM-07 | S | AM-06 | Yes | Console slew test |
| AM-08 | M | AM-03 | Yes | Transformer frequency test, finite on a full-scale sweep, no allocation |
| AM-09 | S | AM-06 | Coordinate. Factory lines in `Devices.cpp` | `createDevice("drift")` returns the device. `nativeDeviceTypes` lists it. A project round-trips the four core parameters. Showcase project is unchanged in this ticket |
| AM-10 | L | AM-04 through AM-09 | Yes, once AM-09's branch is quiet | The section 7 rows that do not need tape or UI are in Catch2 and green under ASan, UBSan, and TSan |

Suggested order inside the milestone: AM-01, then AM-02, AM-03, and AM-04 together, then AM-05, then the modes. AM-02b and AM-04b land with the first integration, not before the library builds.

### Milestone B — tape, after legal

| ID | Size | Depends on | Parallel | Acceptance |
| --- | --- | --- | --- | --- |
| AM-LIC-01 | S | — | Yes. Do this first | GHOVLZ Licensing & Compliance has replied in writing. The reply is pasted into [TAPE_PROVENANCE.md](TAPE_PROVENANCE.md). Until that paragraph exists, AM-11 and AM-12 are not started |
| AM-11 | L | AM-LIC-01, AM-02, AM-04 | Yes, in `src/dsp/analog/Tape.h` only | Behavior spec section 11. Implementer has not opened the GPL tree. Provenance names them |
| AM-12 | M | AM-11 | Yes | Black-box procedure in the spec, section 10. Renders not committed unless the legal reply allows it. Divergences written into the spec, not "fixed" from source |
| AM-13 | S | AM-11 | Yes | Provenance table filled in for implementer and validator |

### Milestone C — UI

| ID | Size | Depends on | Parallel | Acceptance |
| --- | --- | --- | --- | --- |
| AM-14 | M | AM-09 | Coordinate `paintFrame` | Lane matches the wireframe on the software canvas. Non-Drift devices look as they do now. A type-specific branch, not a rewrite of `deviceChain` |
| AM-15 | M | AM-04, AM-14 | Yes | Snapshot triple-buffer. Curves move while audio runs. UI thread does not call `process`. Correlation 1 shows two curves; correlation 0 with offset 0 shows one curve on top of the other |
| AM-16 | S | AM-14 | Coordinate the screenshot PNGs | `--screenshot` writes a device view that includes Drift. `SCREENSHOT_AUDIT.md` gets one row |

### Milestone D — after the EQ/DYN port

| ID | Size | Depends on | Parallel | Acceptance |
| --- | --- | --- | --- | --- |
| AM-17 | L | Milestone A, and the port plan naming the faceplates | No. Blocked on `docs/ports/` | EQ, DYN, or Glue calls `Section` on the full signal behind one new parameter, default off, and nulls against today's device when that parameter is off. Which device goes first is the port plan's choice |
| AM-18 | M | Milestone A | Format 2 is its own change. Do not start it while a format edit is open | `Project::format` 2 reads a per-track analog flag, seed, and trim. Version 1 files load with the flag off. CPU ceiling 0.5% per enabled strip |
| AM-19 | S | AM-18 | Coordinate mixer paint | Gold ANALOG pill, default off, state saved |

Milestone A and the legal ask can start immediately. Milestone B starts after the reply. Milestone C can start as soon as AM-09 has the type id, and should not start earlier if `Paint.cpp` is being rewritten by someone else. Milestone D waits.

## 9. Intellectual property

Implement from published DSP, not from a plugin binary and not from a GPL tree.

| Topic | Source |
| --- | --- |
| ADAA | Parker, Zavalishin, Le Bivic, DAFx-16. Bilbao, Esqueda, Parker, Välimäki, IEEE SPL 2017 |
| Tape hysteresis | Jiles and Atherton 1986. Holters and Zölzer, DAFx-16. Chowdhury, DAFx-19, via the behavior spec only |
| Tape loss, bias, wow method | The same DAFx-19 paper, Bertram, Camras, Kadis, Arnardottir, Abel, Smith AES 125 |
| Wear | Välimäki, González, Kimmelma, Parviainen, JAES 2008, plus the control names in the public ChowTape manual |
| Triode | Dempwolf, Holters, Zölzer, AES 131. Pakarinen and Karjalainen, IEEE TASLP 2010, for the later WDF tier |
| Diode clipper quality tier | Yeh and Smith, DAFx-08 |
| Transformer | Holters and Zölzer, DAFx-16, for the JA tier. The saturating-inductor default is specified in section 2.3 |
| Shelves and biquads | RBJ Audio EQ Cookbook, already the basis of `Biquad.h` |
| Seed generator | xoshiro128**, Blackman and Vigna, public domain |

Clean-room rules for tape:

1. The spec author studies the papers and the public manual and writes [TAPE_BEHAVIOR_SPEC.md](TAPE_BEHAVIOR_SPEC.md). That role is filled. The author did not read the GPL source. The record is [TAPE_PROVENANCE.md](TAPE_PROVENANCE.md).
2. The implementer works from that spec and those papers. They do not open AnalogTapeModel, a fork, or a generated binding. They are a different person or agent from anyone who has read that tree.
3. The provenance file records the implementer and the validator when they are assigned.
4. Validation compares measurements against renders of the free plugin run as its own program. It does not link the plugin. A miss is resolved from the spec. Sample-null is not a goal, and a divergence is written down rather than closed by reading source.
5. AM-LIC-01 asks GHOVLZ Licensing & Compliance to review this before any tape code lands. The ticket blocks AM-11 and AM-12.

Deviation and Topos are commercial products. Their names, artwork, presets, and transfer functions are not assets of this project. The public behavior notes in section 2.8 are the only use.

Do not FetchContent, submodule, or vendor AnalogTapeModel, its JUCE tree, or its weights. A State Transition Network trained by someone else is out of scope for the same reason.
