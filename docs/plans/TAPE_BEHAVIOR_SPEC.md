# Tape behavior spec

Status: clean-room behavior spec. No product code, and no code in this file.

Implementers build the Drift tape mode from this file and from the papers cited in [TAPE_PROVENANCE.md](TAPE_PROVENANCE.md). They do not open the AnalogTapeModel source tree. ChowTape is GPLv3 and JUCE-based. Translating it would put the GPL on GHOVLZ DAW, which this repository refuses (see `tools/check_no_juce.py` and [JUCE_PORTING_NOTES.md](../JUCE_PORTING_NOTES.md)).

The scientific model is J. Chowdhury, "Real-Time Physical Modelling for Analog Tape Machines," DAFx-19, 2019, https://www.dafx.de/paper-archive/2019/DAFx2019_paper_3.pdf (CC BY 3.0). Equation numbers below are that paper's. Where this spec fills a gap the paper left open, the section says so.

This mode is one nonlinearity inside Drift. The stereo motion engine, the static per-side offset, the bit-exact correlation rule, oversampling, DC blocking, and the realtime rules are in [ANALOG_MODE_PLAN.md](ANALOG_MODE_PLAN.md). They apply to tape the same way they apply to the other modes. Azimuth, chew, and noise are extra tape paths and are specified here because they can break the bit-exact rule if they are careless.

## 1. What the listener should hear

At a moderate drive, ferric tape at 7.5 ips:

- Soft saturation that depends on history. A positive pulse and a negative pulse of the same shape do not leave the same state.
- Both even and odd harmonics. Bias near the paper's nominal value keeps the loop from opening into a dead zone. Lower bias widens the loop and puts a flat region around zero, which Camras calls the dead zone.
- High-frequency loss that gets worse as tape speed drops, and a low-frequency head bump that moves down when speed goes up.
- A little pitch drift. Flutter is a few hertz. Wow is slower and less regular.
- With bias engaged and the input silent, a low hiss from the hysteresis of the ultrasonic bias, not from a noise oscillator. That hiss is part of the model. Tests that expect silence turn bias amplitude to zero.
- Chew and degrade are optional wear. At their zero settings the path is a single magnetisation, then loss, then wow. The 2019 paper did not model wear. Section 8 defines an original wear path from Välimäki's antiquing paper and from the control names in the public ChowTape manual.

The model will not null against a Sony TC-260, and it will not sample-null against ChowTape. Section 10 says what "close" means.

## 2. Signal order

One channel, at the oversampled rate:

1. Map the input sample to a record-head field `H` (section 4), then add bias (section 5).
2. Integrate magnetisation `M` from `H` (section 6).
3. Map `M` to a play-head voltage (section 4).
4. Apply the speed-dependent loss magnitude (section 7). The paper's de-biasing lowpass is in this stage.
5. Apply wow and flutter as a modulated delay at the base rate, after decimation (section 8). Pitch drift does not have to run at 16×.
6. Apply chew, then degrade (section 8). Both default to off.

Drift's shared chain wraps this core. Pre-emphasis and de-emphasis sit outside the core, in the shared chain, and are the musical tone control. They are not a substitute for the loss filter. The loss filter stays inside the core because its shape is set by speed, gap, spacing, and thickness.

Stereo is two of these cores. The plan's correlation and offset rules pick the per-channel drive, bias amount, and loss corner. Wow uses one delay modulator when correlation is 0 and two when correlation is 1, so pitch drift itself becomes a stereo difference. Azimuth is a separate fixed inter-channel delay and defaults to 0. The core bit-exact test sets azimuth, chew, and degrade to off.

## 3. Constants

Ferric oxide, as printed in the DAFx paper's section 4.1 and figure 6, from Jiles and from Jiles, Thoelke, and Devine:

| Symbol | Value | Meaning |
| --- | --- | --- |
| `Ms` | 3.5×10^5 A/m | saturation magnetisation |
| `k` | 27×10^3 A/m | loop width, taken as the coercivity |
| `a` | 22×10^3 A/m | anhysteretic shape |
| `c` | 0.17 | reversible fraction |
| `α` | 1.6×10^−3 | domain coupling |

Record head, paper section 4.2.1 and figure 6:

| Symbol | Value | Meaning |
| --- | --- | --- |
| `N` | 100 | turns |
| `E` | 0.1 | head efficiency |
| `g` | 6×10^−6 m | record gap used for the constant field |

Play head, for the loss filter, paper sections 3.3.1 and 4.2.2. These are the user parameters, with these defaults:

| Control | Default | Paper range |
| --- | --- | --- |
| Speed `v` | 7.5 ips (0.1905 m/s) | TC-260 runs 3.75 and 7.5 ips. Also offer 15 and 30 ips as musical choices. The paper's loss figure uses 15 ips. |
| Gap `g` | 5×10^−6 m | Play gap about 1.5 to 6 μm. Record gap default 6 μm if a separate record gap is ever exposed. One user gap is enough for Drift. |
| Thickness `δ` | 35×10^−6 m | Typical TC-260 stock. |
| Spacing `d` | 20×10^−6 m | Upper end of the paper's "as high as 20 microns." |
| Play width `W` | 0.125 in | TC-260 track width. It scales output level only. |

Drive, saturation, and bias knobs scale these constants. They do not replace them.

- Drive multiplies the input current before the record head. Unity drive is the current that produces a peak field of about 5×10^5 A/m for a full-scale input, which is the paper's TC-260 peak-to-peak figure of that order.
- Saturation divides `Ms`. Higher saturation knob, lower `Ms`, more distortion. The manual describes that polarity. Keep the other JA constants fixed in the first version.
- Bias is section 5.

Expose speed, gap, spacing, thickness, drive, saturation, and bias on the expanded tape row. Leave `N`, `E`, `α`, `a`, `c`, and `k` internal. A later fit can vary `k` with the bias knob if the ultrasonic bias is switched off (section 5).

## 4. Record head and play head

The paper's spatial field (Karlqvist, its equations 1–2) is not used. Equation 15 collapses the field to the deep-gap value, and equation 17 is the discrete record head:

```
H[n] = N * E * I[n] / g
```

`I[n]` is the input current in amperes. With `N`, `E`, and `g` above, `H = 1.667×10^6 · I`. Calibrate drive in field units, not by multiplying the paper's "0.1 mA" sentence by this `g`. Those two published figures do not multiply to the paper's stated field of about 5×10^5 A/m, so the spec does not pretend they do. A 0 dB drive and a 0 dBFS sine land on a peak `|H|` of 2.5×10^5 A/m, which is the same order as `Ms` and above the coercivity `k`. That is the region where the loop saturates. The paper's early plot, 800 to 2000 A/m, is the mild end of the same drive knob.

The ideal play voltage, equation 27, is a constant times `M[n]`:

```
V[n] = N * W * E * v * μ0 * M[n] / g
```

`μ0 = 4π×10^−7`. The constant is fixed for a given speed. Apply it, then a makeup so that a very small input (field far below coercivity, bias off) has unity gain through the core within ±1 dB after the loss filter's midband is normalized to 0 dB. The makeup is a constant computed when the loss filter is rebuilt, not a follower inside the solver.

## 5. Bias

Paper equation 28, from Camras:

```
I_head[n] = I_in[n] + B * cos(2π * f_bias * n * T)
```

Paper example: `f_bias` = 55 kHz, `B` about an order of magnitude above the signal current, illustrated as a gain of 5 relative to a unit signal, and a recovery lowpass at 24 kHz. TC-260 bias is 55 kHz.

Rules:

- Run the oscillator at the oversampled rate. `T` is the oversampled sample period.
- Default `f_bias` = 55 kHz. Default relative gain = 5 at the drive calibration of section 4.
- The bias knob scales `B`. Knob at the manual's "normal" position is gain 5. Knob at minimum is `B = 0`, which produces the dead zone. Knob above normal over-biases and reduces distortion.
- The oscillator phase is state. At correlation 0 both channels share one phase (one increment, the sample copied). At correlation 1 each channel has its own phase, started `π/2` apart from the seed, so the ultrasonic tone does not itself correlate. The bit-exact test uses correlation 0.
- Recovery filter: a Butterworth lowpass at 24 kHz, two poles, at the oversampled rate, applied to `V[n]` before decimation. The paper notes that the loss filter already removes most of this energy. Keep the lowpass anyway so a 4× build cannot leak 55 kHz into the baseband by aliasing. If `f_bias` is not at least a factor of two below the oversampled Nyquist, do not run the oscillator. Drop to the fallback in the next paragraph. At 48 kHz and 4×, Nyquist is 96 kHz, so 55 kHz is legal. At 44.1 kHz and 2×, Nyquist is 44.1 kHz, so the oscillator is illegal.
- Fallback when the oscillator is illegal: leave `B` out and multiply `k` by `1 + 4 * (1 − biasNorm)`, where `biasNorm` is 0 at full dead zone and 1 at the normal bias. This widens the loop as bias falls, which is the manual's description of under-bias, without an ultrasonic tone the rate cannot hold. Document the fallback in the device so a test can force it.

The paper uses 16× oversampling so the biased hysteresis stays stable and the bias itself is representable (section 5.1). Drift's quality tape preset is 16× and RK4. The live default preset is 8× and RK2, with the oscillator on at 48 kHz. CPU ceilings are in the plan.

## 6. Magnetisation

### 6.1 Continuous model

Jiles-Atherton, paper equation 6, as a time derivative in equation 18, following Holters and Zölzer:

```
Q = (H + α M) / a

Man = Ms * L(Q)

dM/dt = f(t, M, H, dH/dt)
```

with

```
L(x)     = coth(x) − 1/x          for |x| > 10^−4
         = x / 3                  otherwise

L'(x)    = 1/x^2 − coth(x)^2 + 1  for |x| > 10^−4
         = 1/3                    otherwise
```

The small-argument replacements are paper equations 23 and 24. They exist because `coth(x) − 1/x` cancels at the origin. Use the platform hyperbolic tangent to form `coth = 1/tanh` outside that neighborhood. Do not port a continued-fraction coefficient set.

`δS` is +1 when `H` is rising and −1 when `H` is falling. `δM` is 1 when `δS` and `(Man − M)` have the same sign, and 0 otherwise. When `δM` is 0 the irreversible term is off and only the reversible term remains. Hold the last `δS` when `dH` is exactly 0 so a constant input does not flip the loop.

The derivative the integrator needs, paper equation 18, written out:

```
num = (1 − c) * δM * (Ms * L(Q) − M) / ((1 − c) * δS * k − α * (Ms * L(Q) − M)) * dH/dt
    + c * (Ms / a) * dH/dt * L'(Q)

den = 1 − c * α * (Ms / a) * L'(Q)

dM/dt = num / den
```

If `den` is within `10^−8` of zero, or the denominator of the irreversible fraction is within `10^−8` of zero, skip the irreversible term for that evaluation and keep the reversible term. If that is still singular, hold `M`. Do not emit a NaN. This guard is part of the spec because the paper's RK4 was chosen over the trapezoidal rule specifically to avoid high-frequency blow-ups, and a DAW still has to survive a full-scale transient.

`dH/dt` uses the paper's trapezoidal estimate, equation 21, with `T` the oversampled period:

```
dH/dt[n] = 2 * (H[n] − H[n − 1]) / T − dH/dt[n − 1]
```

State per channel: `M`, `H` previous, `dH/dt` previous, `δS`. All zero at prepare. Reset them when the sample rate or the oversampling factor changes.

### 6.2 RK4

Paper equation 22. `u` is the pair `(H, dH/dt)`. Half-step `u` is the linear interpolation the paper specifies.

```
k1 = T * f(n−1, M[n−1], u[n−1])
k2 = T * f(n−1/2, M[n−1] + k1/2, u[n−1/2])
k3 = T * f(n−1/2, M[n−1] + k2/2, u[n−1/2])
k4 = T * f(n,     M[n−1] + k3,   u[n])

M[n] = M[n−1] + k1/6 + k2/3 + k3/3 + k4/6
```

This is the quality solver. It matches the paper's real-time system.

### 6.3 RK2

The paper does not print an RK2 step. The public manual lists "2nd-order Runge Kutta (RK2)" as a cheaper mode. Use the textbook Heun step on the same `f`. This is not a transcription of the plugin.

```
k1 = T * f(n−1, M[n−1], u[n−1])
k2 = T * f(n,   M[n−1] + k1, u[n])

M[n] = M[n−1] + (k1 + k2) / 2
```

No half-sample `u` is required. RK2 is the default live solver.

### 6.4 NR4 and NR8

The manual lists 4-iteration and 8-iteration Newton-Raphson solvers and says they are more accurate and more expensive than the Runge-Kutta pair. It does not give the residual. Derive it from the paper, as follows. Do not open the plugin to compare Jacobians.

Integrate equation 18 with the trapezoidal rule, which the paper writes down and then sets aside as less stable than RK4 for a biased input:

```
M = M_prev + (T/2) * (f(n, M, u[n]) + f_prev)
```

Residual:

```
g(M) = M − M_prev − (T/2) * (f(n, M, u[n]) + f_prev)
```

Newton, starting at `M_prev`, for exactly 4 iterations (NR4) or exactly 8 (NR8), not "until converged":

```
M := M − g(M) / g'(M)
```

`g'` is the analytic derivative of `g` with respect to `M`. Differentiating `f` is mechanical and is part of the implementer's job: `Q` depends on `M`, `L` and `L'` depend on `Q`, and the quotient rule applies to the irreversible fraction. Where that derivative is singular, replace `g'` with a one-sided difference `g(M + ε) − g(M)` using `ε = 10^−4 * Ms`, still inside the iteration cap. If the step would move `M` by more than `Ms` in one iteration, clamp the step to `Ms`. If `g'` is still unusable, stop the iteration early and keep the last finite `M`.

NR4 and NR8 are opt-in. They are not the default, because the paper's own system shipped RK4, and because this residual is our derivation. Black-box comparison against the plugin's NR modes is informative and is not a sample-null target (section 10).

### 6.5 Out of scope

- The manual's V1 parameterisation. It is a historical variant of the same plugin, not a published model.
- The manual's State Transition Network. That is a neural approximation (Parker, Esqueda, Bergner, DAFx-19). Drift does not train or ship one.
- The paper's section 6 spatial field. The paper says a 100-point spatial grid at 16× would be on the order of 1600 times the cost. It is not a DAW feature.

## 7. Loss filter

Paper equation 13 is the playback magnitude for a sine of frequency `f`. Wave number `k_wave = 2π f / v` (the paper's `k` in section 2.3.2, not the JA coercivity):

```
G(f) = exp(−k_wave * d) * (1 − exp(−k_wave * δ)) / (k_wave * δ) * sin(k_wave * g / 2) / (k_wave * g / 2)
```

As `k_wave → 0`, the thickness term tends to 1 and the gap sinc tends to 1, so `G` tends to 1. Use that limit in the first bin. Spacing loss is the exponential, gap loss is the sinc, thickness loss is the middle factor. Head bump is the low-frequency shape of this product once it is normalized. Doubling `v` halves every `k_wave`, so the bump and the sinc notches move down by an octave. That shift is an acceptance check.

Build a linear-phase FIR by placing `G(f)` on the bins of a length-128 real FFT, mirroring the Hermitian way, and inverse-transforming. The paper's figure uses order 100 at one operating point. 127 taps (128-point FFT) is the Drift size. Normalize so `G` at 1 kHz is 0 dB after the transform, and fold that makeup into the play-head constant of section 4. Rebuild only when `v`, `g`, `d`, `δ`, or the sample rate changes. The FFT workspace and the coefficient array are allocated in `prepare`. The audio thread writes new coefficients into the spare buffer and publishes the pointer. It does not allocate.

The loss FIR runs at the oversampled rate, before decimation, so its cutoff tracks the internal Nyquist. Decimation uses Drift's shared halfband, not a second design.

Azimuth, from the public manual: a misaligned play head delays one channel relative to the other. Implement as a fractional delay of at most 40 samples at the base rate, about 0.8 ms at 48 kHz, scaled by a bipolar knob defaulting to 0. This is a delay, not a change of harmonic character. The core stereo test leaves it at 0 so the measured decorrelation comes from the nonlinearity and the motion engine.

## 8. Wow, flutter, chew, degrade

### 8.1 Wow and flutter

The paper's section 4.4 measured a TC-260 with a pulse train and played the fit through a modulated delay, by the method of Arnardottir, Abel, and Smith. The paper does not print the fit. Those coefficients stay out of this spec.

Drift's delay, at the base rate, after decimation:

- Flutter: one sinusoid. Default rate 6 Hz, default depth 0. Depth knob 1 is ±0.15% speed, which is a few samples at 48 kHz. Rate knob from 0.5 Hz to 20 Hz, with a tempo-sync option on the transport hook from the plan.
- Wow: one sinusoid an octave-decade slower, default 0.6 Hz, plus a drift term. Drift is smoothed noise integrated once, with a time constant of 2 seconds, scaled by the wow variance knob. The noise is the plan's seeded generator. At correlation 0 both channels read the same delay modulation sample. At correlation 1 the right channel uses an independent draw and an independent phase.
- The delay line is allocated in `prepare` to the maximum depth plus one block. Interpolation is linear. No allocation on the audio thread.

Tempo sync and tape-speed sync are multiplies on the two rates. They do not change depth.

### 8.2 Chew

The 2019 paper says wear and dropouts were not characterised. The public manual's chew controls are depth, frequency, and variance: how deep the holes are, how often they occur, and how irregular the spacing is. The behavior below is an original dropout envelope in the sense of Välimäki, González, Kimmelma, and Parviainen, JAES 2008. It is required to produce holes at the selected rate. It is not required to match the plugin's envelope sample for sample.

- An event schedule from the seeded generator. Mean interval is `1 / frequency`. Variance 0 makes the interval exact. Variance 1 lets the interval range from a quarter of the mean to four times the mean.
- Each event applies a raised-cosine dip. Depth 0 is no dip. Depth 1 reaches silence at the bottom of the dip. Dip length is 8 ms at depth 1 and scales down with depth, with a floor of 1 ms.
- At correlation 0 both channels share the schedule. At correlation 1 the right channel has its own schedule from the other stream. The core bit-exact test sets depth to 0, which disables the scheduler.

### 8.3 Degrade

Manual controls: depth (intensity, with a 0.1× fine range), amount (age), variance (time-varying randomness), envelope (noise follows the signal). Same literature as chew. Original filter, not a transcribed one.

- Hiss level in dB is `−60 + 40 * amount * depth`, and the 0.1× switch scales that addition by 0.1. Amount 0 or depth 0 is silence from this stage.
- Hiss is white noise from the seeded generator, high-passed at 2 kHz with one biquad, so it sits as hiss rather than rumble.
- A one-pole lowpass on the audio, cutoff `8 kHz − 6 kHz * amount * depth`, models dull worn tape. Cutoff never goes below 1.5 kHz.
- Variance modulates the hiss amplitude by a smoothed random around 1, depth of modulation equal to the variance knob.
- Envelope on: hiss amplitude also multiplies a 30 ms RMS of the channel. Envelope off: hiss ignores the signal.
- Correlation 0 uses one noise stream, copied. The bit-exact test sets amount and depth to 0.

## 9. Realtime and determinism

- All solver state, delay lines, FIR coefficients, and the bias phase live in memory from `prepare`.
- No locks, no allocation, no system RNG, no clock. The plan's seeded generator is the only random source, and wow, chew, and degrade draw from it in a fixed order so a render repeats.
- Denormal flush on `M`, on the loss FIR state, and on the delay interpolation, with the same threshold the rest of the tree uses (`1e−20`).
- A non-finite `M` resets that channel's solver state to 0 and emits 0 for that sample. It counts as a test failure if a sweep inside ±1 produces one.
- Sample-rate or oversampling changes reset solver state. A transport seek does not, unless the user bypasses and re-enables the device.

## 10. Black-box validation

ChowTape is used as a separate program. It is not a dependency, not a submodule, and not a library loaded into the DAW. Renders stay out of git until Licensing & Compliance says the files are data rather than a derived work of the plugin. The procedure:

1. Standalone or a host that is not GHOVLZ. A stereo WAV, 48 kHz, 24-bit, of a 1 kHz sine at −12 dBFS for 3 seconds, then a sweep, then 2 seconds of silence.
2. Plugin preset written down in the test notes: RK4, oversampling 16×, input gain unity, drive and saturation at the manual's unity marks, bias at the normal mark, wow and flutter at the smallest non-zero depth the UI allows and also at zero, chew and degrade at zero, loss controls at the defaults in section 3, dry/wet fully wet. Record the plugin version.
3. Our render uses the same WAV and the quality preset (RK4, 16×, constants of section 3, bias gain 5, wear off, wow off).
4. Compare measurements, not samples. Targets for the first tape milestone, on the sine:
   - Level after makeup within ±2 dB of the plugin.
   - The first five harmonics each within ±4 dB of the plugin's harmonic, relative to the fundamental.
   - Alias energy, as defined in the plan, within ±6 dB of the plugin's alias energy and still under the plan's tape ceiling.
   - On the sweep, the frequency where the loss response has fallen 3 dB from 1 kHz agrees within a third octave.
   - With bias at minimum and the oscillator legal, a 1 kHz sine shows a visible flat spot around the zero crossings in a waveform plot, on both renders.
   - With the input silent and bias at normal, both renders have a noise floor above a bypassed render, and the noise is not a tone at a bin below 20 kHz.
5. A miss is investigated by re-reading this spec and the papers. It is not investigated by reading the plugin source. If a target is wrong because this spec's NR residual or wear envelope differs from the plugin, record the divergence here and keep our definition. Sample-null is not a goal.
6. RK2, NR4, and NR8 are compared the same way, once, and the differences are written into this file. They are not release blockers unless RK4 itself misses the targets above.

## 11. Acceptance

Tape mode is done when:

- The plan's core stereo tests pass on the tape mode with azimuth, chew, and degrade at zero.
- A history test passes: process a positive unit impulse, then a 200 Hz sine; reset; process a negative unit impulse, then the same sine. After 50 ms the two sine tails differ by more than 0.5 dB in the second harmonic, or the residual `M` differs. A memoryless waveshaper fails this test.
- Doubling speed moves the head-bump peak down by an octave, ±20%.
- Bias at minimum widens the time-domain dead zone relative to normal bias, on a 1 kHz sine.
- RK4 at 16× stays inside the plan's CPU ceiling on the CI runner, or the ceiling is revised in the plan with the measured number attached.
- AM-LIC-01 is answered in the provenance file before the code is merged to `main`.
