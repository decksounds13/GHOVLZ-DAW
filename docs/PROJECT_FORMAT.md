# Project format

A GHOVLZ project is a folder. Version 1 of the format is what `Project::saveToFolder` writes and `Project::loadFromFolder` reads. There is no ValueTree and no XML.

```text
MySong/
  project.json
  audio/<media-id>.wav
```

`project.json` is UTF-8 JSON, indented. Audio files are stereo or mono 24-bit PCM WAV at the media's own sample rate. The engine plays them at the project rate.

## Root object

| Field | Meaning |
| --- | --- |
| `format` | Integer. `1` for this version. |
| `name` | Project name. |
| `bpm` | Quarter-note tempo. |
| `timeSignature` | `[numerator, denominator]`, for example `[4, 4]`. |
| `loop` | Whether transport loops. |
| `loopStart`, `loopEnd` | Loop range in quarter notes. |
| `playhead` | Transport position in quarter notes. Bar 1 beat 1 is `0`. |
| `metronome` | Boolean. |
| `metronomeLevel` | Linear click level. |
| `scenes` | Array of scene names, in slot order. |
| `markers` | `{name, quarter, color}`. `color` is `#RRGGBB`. |
| `macros` | Eight numbers, the performance macros. |
| `master` | The master track, same shape as a track. Not inside `tracks`. |
| `tracks` | Audio, MIDI, group, and return tracks. |
| `automation` | Lanes. A lane id looks like `track:<track-id>:gain` or `track:<track-id>:pan`. Points are `{q, v}` in quarter notes and the parameter's own unit (decibels for gain). |
| `media` | `{id, file, sampleRate, channels}`. `file` is relative, `audio/<id>.wav`. |

## Track

`type` is `audio`, `midi`, `group`, `return`, or `master`.

Also stored: `id`, `name`, `output` (usually `master` or a group id), `volumeDb`, `pan` from -1 to +1, `mute`, `solo`, `arm`, `color`, `devices`, `clips`, `slots`, `sends`.

A send is `{target, db, pre}`. `pre` true taps the signal before the fader.

A slot is a clip or JSON `null`. Slot index matches `scenes`.

## Clip

`id`, `name`, `startQ`, `lengthQ`, `offsetQ`, `mediaId`, `color`, and `notes`. A note is `{startQ, lengthQ, pitch, velocity}` with pitch as a MIDI note number and velocity from 0 to 1. Note times inside an arrangement clip are relative to the clip start. Audio clips name a `mediaId`.

## Device

`id`, `type`, `name`, `bypass`, `pluginPath`, `state`, `params`.

`type` is a native id (`eq`, `dyn`, `compressor`, `limiter`, `gate`, `saturator`, `filter`, `delay`, `reverb`, `chorus`, `flanger`, `phaser`, `utility`, `analyzer`, `wavetable`, `sampler`, `drumrack`, `arpeggiator`, `chord`, `velocity`) or `vst3`.

`params` is a map of parameter id to a plain number. EQ band ids are `onN`, `typeN`, `freqN`, `gainN`, `qN` for N from 1 to 8, plus `phase`, `side`, `sideAmt`, `sideHp`, `sideLp`, `lfoRate`, `lfoDepth`.

`state` is a string. For a VST3 device it is the plugin's `IComponent::getState` stream, base64-encoded. Native devices keep their values in `params`.

## Round trip

Saving and loading restores tracks, devices, parameters, clips, notes, scenes, markers, automation, macros, and the audio files. The test `project round trip` covers a track volume and an EQ gain. `offline wav render` writes a 24-bit stereo 48 kHz file from the showcase project.
