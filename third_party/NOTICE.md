# Third-party notices

GHOVLZ DAW v0.1 does not vendor JUCE, Gin, melatonin_blur, or the ASIO SDK.

## Fetched at configure time

| Library | Version | License |
| --- | --- | --- |
| Catch2 | 3.7.1 | Boost Software License 1.0 |
| nlohmann/json | 3.11.3 | MIT |
| RtMidi | 6.0.0 | MIT. Copyright (c) 2003-2023 Gary P. Scavone |
| miniaudio | 0.11.22 | Public domain or MIT-0, at the user's choice. David Reid |
| stb (stb_image, stb_image_write, stb_truetype) | commit 9600522 | Public domain. Sean Barrett |
| VST3 SDK | tag `v3.8.1_build_84` | MIT. Copyright (c) 2026 Steinberg Media Technologies GmbH |

The VST3 SDK tag above is the MIT release. The 3.7 line is dual-licensed proprietary or GPL and is not used. This is an internal build. Do not redistribute the SDK outside Steinberg's terms. The SDK is downloaded by CMake; it is not committed in this repository.

Windows system libraries used by the app and not shipped here: Direct2D, DirectWrite, Direct3D 11, DXGI, WIC, WASAPI, the Windows SDK.

## Not fetched

**ASIO SDK 2.3.4** (Steinberg Media Technologies). Not committed. On Windows, CMake downloads `ASIO-SDK_2.3.4_2025-10-15.zip` into the build tree, or `GHOVLZ_ASIO_SDK_DIR` points at a local tree that contains `common/asio.h`. The SDK's terms do not allow redistribution from this public repository. A closed-source binary that links ASIO still needs Steinberg's proprietary licence. WASAPI remains the default device. Our host is `src/audio/AsioBackend.cpp`. `tools/check_no_asio_sdk.py` fails the build if the SDK files are committed.

## Behavior ported, code not included

**melatonin_blur.** MIT, Copyright (c) 2023 Sudara Williams. Drop shadows, inner shadows, and blurred panels are reimplemented with Direct2D's shadow and Gaussian-blur effects, and with a cached mask on the software canvas. The JUCE module is not a dependency.

**Gin.** BSD-3-Clause, Copyright (c) 2018 Roland Rabien. Knob, slider, button, meter, and panel behavior is redrawn against the GHOVLZ palette and the DarkKnob filmstrip. The JUCE module is not a dependency.

The MIT and BSD-3 notices above are the licenses those projects publish. Their source is not copied into this tree.

**EQ colour dice.** `src/ui/ColourDice.h` is a seeded re-expression of the EQ tool's ramp randomizer: 2 to 10 stops, an HSV-limit walk, and the same ordered or scattered layout. That tool draws from an unseeded system generator. This tree does not include that source. The generator here is a short PCG XSH-RR (Melissa O'Neill's published algorithm) so a seed reprints the ramp.

## Algorithms re-expressed from GHOVLZ-EQ

EQ, multiband dynamics, and analyzer behavior follow the public repository https://github.com/decksounds13/GHOVLZ-EQ at these commits:

- `main` `c432bdf6db3b4eb7269cc3ff7af675536098db7b` parametric EQ, linear phase, SideCheck, LFO
- `dyn/main` `5cafeb625fffb0093256081a893b6bf8169ac428` multiband dynamics
- `analyzer/main` `bde1c9820cccf521437ad320fd4ae4358b835dc9` analyzer

The EQ repository's license file was not available when this port was written (the license URL returned 404). Do not assume a license. The original repository should be consulted before reuse outside this internal build. The DSP in `src/dsp` is a re-expression on this project's own buffer and parameter types. Filter coefficients follow the Audio EQ Cookbook (Robert Bristow-Johnson). Provenance is this paragraph, not a copied header from that repository.

## Artwork in tree

`assets/DarkKnob4_Stitched.png` and `assets/pirulen.ttf` are GHOVLZ assets used by the device knobs and the wordmark.
