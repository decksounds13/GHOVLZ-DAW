# Screenshot audit

PNGs in this folder were written by `GHOVLZ DAW --screenshot` at 1680×980. Linux writes the software canvas. Windows writes the Direct2D target through `openRenderer` and reads the pixels back; `--force-warp` is the CI path. The paint calls are the same on both. The concept boards are not stored in the repository. The comparison is against the brief: near-black field, warm brown-gray, antique gold and amber, Bahnschrift-style wordmark, track names, markers, scenes, and the DarkKnob filmstrip.

This pass measured arrangement paint at 13.7 ms cold and 12.9 ms warm (`frame-times.txt`). AddressSanitizer, UndefinedBehaviorSanitizer, and ThreadSanitizer were run on the Catch2 suite. The 10 minute showcase at 256 frames reported zero audio-thread allocations, and 10,000 graph republishes during playback were ThreadSanitizer-clean.

## Concept against this build

| View | Concept ask | This build |
| --- | --- | --- |
| Arrangement | Headers for Kick, Drums, Snare, and Bass stay visible. Clips do not cover them. Waveforms, MIDI previews, title bars, a filter-cutoff lane, loop brace, Intro/Verse/Drop/Break, gold playhead flag at 17.1.1. | Headers sit left of the clip lane and the lane is clipped. Kick Loop, Drum Loop, Snare, Bass Loop, Lead, Pad Chords, Vocal, and Riser are drawn with waveforms or note previews. The cutoff curve is on Pads. Markers and the 17.1.1 flag are on the ruler. The right column has a mini knob, M/S/Arm, and a stereo meter. |
| Transport | Rounded dark cluster, play, stop, record, loop, metronome, wordmark, readable renderer badge. | Play triangle, stop square, record dot, loop, and metronome are drawn glyphs. The badge reads CPU on these PNGs. |
| Browser | Crisp "BROWSER" and a selected Sounds row. | Text is drawn after any blur, with an amber mark on Sounds. |
| Session | Scenes Intro, Verse, Build, Drop, Break, Drop 2, Outro, End, not covered by the clip grid. | Eight clip columns stop before a 120 px scene column. Drop is the gold outline. |
| Perform | Performance pads. | 8×3 pads. Drop is the gold pad. Pads do not launch clips yet. |
| Mixer | Pan, sends, insert slots, M/S/Arm, routing, dB scale, peak hold, clip lamp, wider stereo meters, channel colour. | Each strip has routing, three insert slots, a pan knob, A/B sends, a fader, a dB scale, a stereo meter, and M/S/Arm. Synth Lead has the gold outline. |
| Device chain | Framed devices, meters, titles. | Wavetable, Filter, and Saturator for Synth Lead, with titled panels, filmstrip knobs, and stereo meters. |
| Piano roll and plugin | Notes, and a generic plugin editor. | Piano roll shows the lead clip. The plugin page is the hosted GHOVLZ Test Gain parameter. |

## Still open in the UI

- Clip drag, zoom, and slip are not implemented.
- A plugin `IPlugView` is not embedded. The page is the host's parameter list.
- Performance pads do not fire clips.
- Browser rows are not dragged onto tracks.
- These PNGs show the CPU badge. `D3D11` and `WARP` are the strings `Direct2DCanvas` returns after device creation on Windows.
