# Tape mode provenance

This note is the record of who studied what for the Drift tape mode. It is part of the clean-room process in [ANALOG_MODE_PLAN.md](ANALOG_MODE_PLAN.md). Update it when a role is filled or when Licensing & Compliance answers.

## Roles

| Role | Who | When | What they did |
| --- | --- | --- | --- |
| Spec author | Cursor cloud agent, run `bc-59cd905e-dc71-566e-b120-3a4ca8005f02` | 2026-11-19 | Wrote [TAPE_BEHAVIOR_SPEC.md](TAPE_BEHAVIOR_SPEC.md) from the publications and the public manual listed below. No product code. |
| Implementer | Unassigned. Must be a different person or agent from anyone who has read the GPL tree. | — | Builds tape mode only from the behavior spec and the papers it cites. |
| Black-box validator | Unassigned. May be the implementer. | — | Renders the free ChowTape plugin as a separate program and compares measurements. Does not open its source. |
| Licensing & Compliance | GHOVLZ Licensing & Compliance. Not yet asked. | — | Ticket AM-LIC-01. Tape code does not land before their written answer is pasted into the review section below. |

The spec author has not cloned, browsed, or read the AnalogTapeModel source tree, its headers, its tests, or any fork of that tree. Search hits that named the repository were not opened. A third-party README that restated plugin features was not used as a specification.

## Sources the spec author used

Published papers and books:

- J. Chowdhury, "Real-Time Physical Modelling for Analog Tape Machines," Proc. DAFx-19, Birmingham, 2019. https://www.dafx.de/paper-archive/2019/DAFx2019_paper_3.pdf . Creative Commons Attribution 3.0 Unported. Equations below are restated from this paper and cited by its equation numbers.
- M. Holters and U. Zölzer, "Circuit Simulation with Inductors and Transformers Based on the Jiles-Atherton Model of Magnetization," Proc. DAFx-16, Brno, 2016. https://www.dafx.de/paper-archive/2016/dafxpapers/08-DAFx-16_paper_10-PN.pdf
- D. C. Jiles and D. L. Atherton, "Theory of ferromagnetic hysteresis," J. Magnetism and Magnetic Materials, vol. 61, pp. 48–60, 1986. The 1984 J. Applied Physics form is the one the ChowTape manual names; the DAFx paper's reference list points at the 1986 journal article.
- D. C. Jiles, J. B. Thoelke, and M. K. Devine, "Numerical determination of hysteresis parameters for the modeling of magnetic properties using the theory of ferromagnetic hysteresis," IEEE Trans. Magnetics, vol. 28, pp. 27–35, 1992.
- D. Jiles, *Introduction to Magnetism and Magnetic Materials*, CRC Press, 2015. Source of the ferric-oxide constants the DAFx paper quotes.
- H. N. Bertram, *Theory of Magnetic Recording*, 1994. Record-head and playback integrals.
- M. Camras, *Magnetic Recording Handbook*, Van Nostrand Reinhold, 1987. Bias and the dead zone.
- J. Kadis, *The Science of Sound Recording*, Focal Press, 2012. Loss terms and the TC-260 context.
- S. Arnardottir, J. S. Abel, and J. O. Smith III, "A digital model of the Echoplex tape delay," AES Convention 125, 2008. Modulated delay for wow and flutter. The DAFx paper says its timing model follows this method.
- V. Välimäki, S. González, O. Kimmelma, and J. Parviainen, "Digital audio antiquing — signal processing methods for imitating the sound quality of historical recordings," J. Audio Eng. Soc., vol. 56, pp. 115–139, 2008. Published dropout, wow, and noise methods. Chew and degrade in the spec are built from this paper plus the manual's control names, because the 2019 tape paper says it did not model wear.
- H. S. Wall, *Analytic Theory of Continued Fractions*, Chelsea, 1948. Cited by the DAFx paper for a tanh approximation. This spec uses the platform `tanh` and the paper's small-argument series instead of copying a rational fit.
- J. D. Parker, F. Esqueda, A. Bergner, and others, "Modelling of Nonlinear State-Space Systems Using a Deep Neural Network," Proc. DAFx-19, 2019. Cited here only to exclude it. The manual's State Transition Network points at this work. Drift does not ship a trained network or anyone else's weights.

Public documentation of behavior, not source:

- ChowDSP, *ChowTape User Manual*, https://chowdsp.com/manuals/ChowTapeManual.pdf . Read for control names, signal-flow order, and the audible intent of bias, loss, wow, flutter, chew, and degrade. The manual states that the plugin is GPLv3. It was not treated as a license to translate the implementation.
- J. Chowdhury, "Analog Tape Emulation for Music Production," ASA 2022 slides, https://ccrma.stanford.edu/~jatin/slides/ASA_Tape_2022/ . Same ODE as the paper, in slide form.

## Sources the spec author did not use

- https://github.com/jatinchowdhury18/AnalogTapeModel and every fork, mirror, and generated binding of it.
- Any file named like the plugin's hysteresis, loss, wow, chew, or degrade processors.
- Preset files shipped with the plugin.
- Coefficients measured out of the plugin binary.

The DAFx paper links the GitHub tree and says the real-time system was written with JUCE. That sentence is why this mode is clean-roomed. Following the link is a breach of the process.

## What is original in the spec

The magnetisation ODE, the ferric constants, the record-head gain, the playback loss magnitude, the bias tone, the RK4 step, and the 16× oversampling argument are the paper's, attributed above.

The following are specified here so an implementer does not have to open the plugin:

- RK2 as the textbook Heun step on the same ODE.
- NR4 and NR8 as a fixed-iteration Newton solve of the trapezoidal residual of that ODE. The manual names the iteration counts and does not give the residual. The residual in the behavior spec is derived from the paper's equation (18) and equation (21). It is not claimed to match the plugin's Jacobian.
- Wow and flutter as a modulated delay whose rates are user parameters. The paper's TC-260 pulse-train fit is not printed as coefficients in the paper text, so those coefficients are not in this spec and must not be lifted from the repository.
- Chew and degrade as antiquing-style dropouts and hiss with the manual's control names. The 2019 paper explicitly did not model wear.
- V1 hysteresis and the State Transition Network are out of scope.

## Review

Licensing & Compliance answer: pending. Ticket AM-LIC-01.

Paste the written answer under this heading. Until it is here, no tape processor is added under `src/`.
