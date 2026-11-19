# Screenshot audit

PNGs in this folder were written by `GHOVLZ DAW --screenshot` on the software canvas at 1680×980. The same paint path is what Direct2D runs on Windows. The concept boards that started the layout are not stored in the repository; the comparison below is against the GHOVLZ identity sheet and the named arrangement from the brief (track names, markers, scenes, pad names, palette).

The app was also opened under Xvfb. ALSA has no card on this machine, so the audio device stays off and the window loop still paints. That run was stopped from outside after it had been up; it did not abort on the missing device or the missing MIDI port.

## Frame time

Software canvas, arrangement, cache already warm from the screenshot pass: about 16.9 ms for the first 30 frames and 16.1 ms for the next 200. See `frame-times.txt` and the renderer section of [JUCE_PORTING_NOTES.md](JUCE_PORTING_NOTES.md). Direct2D was not timed here.

## Views

| File | What it shows | Against the concept |
| --- | --- | --- |
| `arrangement.png` | Browser, ruler with Intro / Verse / Drop / Break, loop across the arrangement, clips named Kick Loop, Drum Loop, Snare, Bass Loop, Lead, Pad Chords, Vocal, Riser, playhead at 17.1.1 (quarter 64 at 128 BPM, 4/4), Synth Lead device strip with DarkKnob filmstrips | Track names, marker names, and the gold/amber clip colors match the brief. The ruler is functional rather than a copy of a concept illustration. |
| `session.png` | Eight tracks by scenes Intro, Verse, Build, Drop, Break, Drop 2, Outro, End, with scene launch buttons | Scene names match. Returns are not given slots in the showcase. |
| `perform.png` | 8×6 pads. Row 0 and row 1 use the names from the brief. One row is teal `#1F6B62`, one is green `#5AA05A`, the rest stay in the warm palette | Pad names for the first two rows match the readable concept labels. Later rows are filled with kit and macro names so the grid is not blank. |
| `mixer.png` | Kick, Drums, Snare, Bass, Synth Lead, Pads, Vocals, FX, A Reverb, B Delay, Master. Gold meters, faders, dB readouts. Kick is boosted in the screenshot pass so its meter is the hot one | Channel list matches. Meters are fed by a real `Engine::process` of the showcase, not drawn from constants. |
| `device-chain.png` | Wavetable, Filter, and Saturator for Synth Lead, with Position / Wave / Unison / Level, Cutoff / Reso / Drive / Mix, Drive / Tone / Mix / Output, plus the EQ Eight magnitude curve | Knob set matches the brief. The curve is `eqMagnitudeDb` on the message thread (default bands, including the gentle bell and shelf). |
| `piano-roll.png` | Synth Lead notes on a piano grid, C labels on the octave | The pitches are the showcase clip (64, 67, 71, 74, 72, 67, 69, 64 and the repeats). |
| `browser.png` | Sounds, Drums, Instruments, Audio Effects, MIDI Effects, Samples, Plugins, with the native device list under Audio Effects | Categories match the brief. Drag and drop onto a track is not implemented; the list is the browser. |
| `plugin.png` | Generic editor for the hosted GHOVLZ Test Gain VST3, Gain at its default normalized 0.5 | The test plugin has no `IPlugView`. The window is the host's parameter page, loaded from the bundle the build just produced. |

## Gaps still visible

- No vendor plugin editor. A plugin with `IPlugView` is not embedded yet.
- The arrangement does not zoom, slip, or drag clips.
- Session launch is implemented in the engine (`launchScene`) and the scene buttons are drawn; the screenshot pass does not click them.
- Mixer meters are peak bars. There is no gain-reduction meter on the strip.
- The performance grid does not fire clips yet. It is the layout and the names.
- Browser items are not dragged onto tracks.
- Linux cannot show the `D3D11` / `WARP` badge. Those strings are what `Direct2DCanvas::backendName` returns after device creation on Windows.
