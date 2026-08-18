# Sampler completion + Sample Editor

**Status:** specification. Nothing here is implemented yet.
**Supersedes:** `M8_SAMPLER_COMPLETION_SPEC.md` Phases 2 and 3, which sketched this
work in a paragraph each. Phase 1 of that document is **done** (2026-07-17) and
Phase 4 is **mostly done** (ZDF LP/HP and LIM POST/POST:AD landed). Keep that
file for its Phase 1 record; this one replaces its forward-looking half.
**Hardware evidence:** measured on a real M8 (fw 6.5.2, COM3) on 2026-08-17 and
2026-08-18. Every screen coordinate, enum, cursor chain and ratio in this
document was read off the device.

**All of it is pinned to firmware 6.5.2.** If Dirtywave changes behaviour in a
later firmware, these findings describe the old one — which is why every
measurement here carries its firmware and date. Re-check before assuming a
finding still holds on a device that has been updated.

**Every open question in §11 is measurable.** None of them depends on
information only Dirtywave has; each is a number somebody can go and read off
the device, and §11 says how for each one.

---

## 1. What is already implemented

Established by reading the tree, not from memory. This is the half of the ask
that is "see what is already implemented".

### 1.1 Working end to end

| Area | Where | State |
|---|---|---|
| `SamplerState` — every instrument-screen field | `src/engine/Engine.h:41` | Loads, renders, edits, **saves**. Sealed 2026-07-17. |
| PLAY modes `00`–`08` | `src/engine/SamplerEngine.cpp:53` | FWD / REV / FWDLOOP / REVLOOP / FWD PP / REV PP / OSC / OSC REV / OSC PP all render. |
| Chromatic note tracking | `SamplerEngine.h:9` (`kSamplerRootMidi = 60`) | C-4 → 262 Hz, C-5 → 525 Hz. |
| DETUNE | `kDetuneSemisPerStep = 1/16` | `0x80` centre; first hex digit = semitones, second = 1/16ths. |
| DEGRADE | `SynthVoice.cpp` `applyDegrade` | Sample-rate reduction, shared decimator phase. |
| FILTER `00`–`04`, `06`, `07` | `SynthVoice::applyFilter` | Includes the ZDF LP/HP pair. `05` (LP>HP) passes through. |
| LIM `00`–`05` | `SynthVoice::applyLimiter` | `06`–`08` (POST:W1–W3) fall back to hard clip. |
| **Stereo** sample playback | `SynthVoice::renderFrame` | Verified against hardware (`S-ST1`/`S-ST2`). |
| Sample load from disk | `FileBrowser::loadWavFile:298` | dr_wav → `SamplePool` (128 slots, refcounted, GC ring). |
| SAMPLER screen | `InstrumentSamplerLayout.h` | All fields present and editable. |

### 1.2 Present but inert

- **`SLICE`** — the byte loads, renders on screen and saves. Playback ignores it
  completely; `grep slice src/engine/` finds it only in `SongCleanup` and the
  param updater. The encoding *is* hardware-verified (§3).
- **PLAY `09`–`0E`** (REPITCH / REP.REV / REP.PP / REP.BPM / BPM.REV / BPM.PP) —
  `SamplePlayMode` names them and `isReverseMode`/`isPingPongMode` include them,
  so they inherit direction and ping-pong. Nothing repitches to tempo and
  nothing time-stretches; `isLoopMode` excludes them, so they play once.

### 1.3 Absent entirely

- **The sample editor.** No `ViewType` for it (`ViewManager.h:10` lists 18
  screens; none is a sample editor), no layout header, no screen file.
- **Recording.** There is no audio input anywhere. `src/main.cpp:347` opens
  `SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK` and nothing else. The engine has no
  capture path, no input ring, no record buffer.
- **WAV metadata.** `loadWavFile` uses
  `drwav_open_file_and_read_pcm_frames_f32`, which discards everything but PCM.
  Cue points (slice markers) and the sample/loop chunk are dropped on the floor.
  `dr_wav.h` **does** support them (`drwav_metadata_type_cue` is in the vendored
  copy) — we simply do not ask for them.
- **Sample preview** in the browser, and the **name/EDIT** state of the SAMPLE
  row (§5).

---

## 2. Scope and order

Five deliverables. They are independent; do them in this order, and **do not
attempt more than one per pass**.

| # | Deliverable | Size | State |
|---|---|---|---|
| A | SLICE playback, equal divisions | small | **DONE** |
| B | SAMPLE row shows the sample name + EDIT | small | **DONE** |
| C | WAV metadata: cue chunk in, loop region in/out | medium | **DONE** |
| D | The sample editor screen (no recording, no processes) | large | **DONE** |
| E | PROCESS actions | large | **DONE** |
| F | Recording | large | needs an audio input path |

| G | PLAY `09`–`0E` (REPITCH / BPM) | medium | partly — one constant still open |

PLAY `09`–`0E` was blocked on an unmeasured tempo law. It is now **mostly
measured** (§G): the shape of the law is settled and the mode repitches rather
than time-stretches. One absolute constant remains open; §G says exactly which,
and how to close it.

---

## 3. A — SLICE playback

### 3.1 The encoding, hardware-verified

Read off the device screen 2026-07-17 (recorded in `status.md`):

| Byte | Meaning |
|---|---|
| `00` | OFF |
| `01` | FILE — use the markers embedded in the WAV (needs C) |
| `02`–`0x80` | 2–128 equal divisions; **the byte value is the slice count** |

**Slice index = the MIDI note number, starting at 0** — measured 2026-08-18
(§9.5). Note 0 plays slice 0, note 1 slice 1, and so on. Notes at or above the
slice count are **silent**: they neither wrap nor clamp.

`status.md` currently says the base is "C-1 = MIDI 24, derived from C-4=60".
That derivation is **wrong** and this measurement replaces it. The device's
lowest displayed octave maps to MIDI 0 in the keyjazz numbering, not to 24.

### 3.2 What to implement

Only the equal-division case (`02`–`0x80`). `01` (FILE) depends on deliverable C
and is a separate change.

In `SamplerEngine::computeRegion`, when `s.slice >= 2`:

```cpp
    const int count = std::clamp(s.slice, 2, 128);
    const int idx   = noteMidi;                    // slice index IS the note
    if (idx >= count) { m_finished = true; return; }   // out of range == silent
    const int32_t sliceLen = frames / count;
    m_startFrame = idx * sliceLen;
    m_loopStart  = m_startFrame;
    m_loopEnd    = m_startFrame + sliceLen;
```

Note the early-out. A note past the last slice produces **nothing** on hardware
(§9.5) — do not clamp it to the last slice, which is the obvious-looking thing
to write and is audibly wrong.

`computeRegion` does not currently receive the note. It is called from
`SamplerEngine::noteOn` and from `SynthVoice::noteOn`; both have the frequency
but not the MIDI note. **Pass the MIDI note down** rather than deriving it back
from frequency — a derived note is wrong the moment DETUNE or a table transpose
is in play, and slice choice must follow the written note, not the sounding
pitch.

### 3.3 START is ignored when slicing — measured

Slices divide the **whole file**. `START` has no effect on them: captured at
SLICE `04`, note 0, with START `00` and START `40`, the two recordings align at
**r = 0.948** (§9.6) — the same audio, within the jitter of capturing a
percussive sample twice. Meanwhile slices 1, 2 and 3 correlate with slice 0 at
only 0.08–0.23, so the four regions really are four different quarters.

So `computeRegion` should compute the slice region from `frames` and ignore
`s.start` entirely when `s.slice >= 2`. `LENGTH` was not tested separately;
treat it as ignored too and say so in a comment.

---

## G — PLAY `09`–`0E` (REPITCH / BPM)

Measured 2026-08-18 (§9.8). The manual says only "Pitches the sample based on
the current song tempo", which left three things unknown: whether it repitches
or time-stretches, what `STEPS` does, and the exact ratio.

### G.1 What was settled

**It repitches; it does not time-stretch.** Captured the same note at 120 and
240 BPM, then time-stretched the 240 capture by 2× and correlated it against the
120 capture: **r = 0.918**. The competing hypothesis — pitch preserved, duration
halved — predicts matching raw spectra, which score only 0.609. At double tempo
the sample plays twice as fast, an octave up.

**It loops while the note is held.** The test sample is a ~0.2 s one-shot; under
REPITCH it filled the entire 4 s capture. So `isLoopMode()` in `SamplerEngine.h`
is **wrong for `09`–`0E`** — it returns `v >= 2 && v <= 8`, which excludes them,
so today they play once. They must loop.

**The loop period scales exactly as `STEPS / BPM`.** Both exponents are 1, to
three digits:

| Change | Predicted | Measured |
|---|---|---|
| tempo ×2 (120 → 240) | period ×0.5 | **×0.501** |
| STEPS ×0.5 (`0x80` → `0x40`) | period ×0.5 | **×0.500** |

So:

```
    loopBeats = k * STEPS          // independent of tempo and of sample length
    loopSeconds = loopBeats * 60 / BPM
    playbackRate = sampleFrames / (loopSeconds * sampleRate)
```

### G.2 The constant that is still open

Measured: at `STEPS = 0x80` and 120 BPM the loop period is **0.2298 s**, which is
0.460 beats, or **1.84 sixteenth-steps**. That gives `k = 0.460/128 = 0.003594`
beats per STEPS unit. Re-measured 2026-08-18 with a better estimator: 229.812 ms,
r = 0.884 — the same number, so the value is solid *for this sample*.

1.84 sixteenths is suspiciously close to 2, and `k = 1/256` would make
`loopBeats = STEPS/256` — exactly 0.5 beats at `0x80`. But the measurement is 8%
below that and **the gap is unexplained**, so do not code `STEPS/256` on the
strength of it looking round.

The ratios are trustworthy (two independent halvings landed on 0.501 and 0.500);
it is only the absolute that is soft. The likely culprit is the measurement, not
the device: the loop period came from autocorrelating the envelope of a sustained
sample, where the true loop point is not sharply defined.

**An attempt to close it failed, 2026-08-18.** An earlier draft of this section
said to use a percussive sample — `BASSDRUM1.WAV` — on the theory that a sharp
transient makes the loop point unambiguous. **That advice was wrong and has been
removed.** Four captures at `STEPS` `0x20`/`0x40`/`0x80`/`0xC0` produced
recordings with **no periodicity at all** anywhere between 1 ms and 400 ms
(normalised autocorrelation never exceeds 0.5). The drum is only 1100 frames;
under REPITCH at note 60 it is squeezed into a few milliseconds and becomes
continuous noise rather than a repeating hit. There is no loop left to measure.

Worse, the first analysis of those captures *appeared* to give a beautiful
1 : 2 : 4 : 6 result. It was an artifact: each search window had been centred on
a prediction scaled by STEPS, every "peak" landed exactly on its window's lower
bound, and the ratio recovered was the ratio of the windows, not of the audio.
**Any peak sitting on a search bound is not a measurement.** Check for that
explicitly before believing a period.

What *does* work is the sustained sample. `ANALOGSTRING.WAV` (8800 frames) gives
genuine interior peaks — 229.8 ms / 115.2 ms / 57.7 ms at r = 0.88–0.97, all far
from their window edges — and re-measuring it with the better estimator
reproduced the law exactly (tempo ×2 → ×0.5014; STEPS ÷2 → ×0.5005).

**What to try next.** The reason no round number has appeared may be that the
question is wrong. If REPITCH simply sets a *playback rate* from tempo and STEPS,
then the loop period is `sampleFrames / rate` and is **proportional to the sample
length** — in which case `loopBeats = k * STEPS` is the wrong model, there is no
musical constant to find, and what needs measuring is the rate law instead.

That is one experiment: capture two samples of **very different lengths** at
identical settings. If the periods are in the same ratio as the sample lengths,
the model above is wrong and should be rewritten as a rate law. `ANALOGSTRING`
(8800 frames, 229.8 ms at `STEPS 0x80` / 120 BPM) is already one data point; pick
a second sample several times longer, and make sure it is long enough that its
loop period stays well above its own pitch period.

### G.3 Untested

Only `09` (REPITCH) was measured. `0C`–`0E` (REP.BPM / BPM.REV / BPM.PP) are the
*BPM* family and the manual names them separately, so **do not assume they share
the law** — the obvious hypothesis is that REPITCH repitches while BPM
time-stretches, and the 2×-stretch test above discriminates them in one capture
pair. Run it before implementing `0C`–`0E`.

`STEPS` is stored somewhere in `SynthParams`; `status.md` guesses
`synth_params.pitch` and says so is unconfirmed. Confirm it by round-tripping a
`.m8s` with a known `STEPS` value before wiring the UI to it.

---

## 4. B — the SAMPLE row

Measured (§9.2): with no sample loaded the device draws

```
 SAMPLE  LOAD             REC.
```

and with one loaded

```
 SAMPLE  ALIEN            EDIT
```

Ours draws `LOAD` and `REC.` unconditionally — the strings are literals in
`InstrumentSamplerLayout.h:51,53` and `ResolveInstrumentValue` returns a constant
`"LOAD"` (`InstrumentScreen.cpp:141`).

Fix, entirely inside the resolvers:

- `C::SAMPLE_LOAD` returns the sample's **base name** (no directory, no `.wav`)
  when `inst.sampler.samplePath[0] != '\0'`, else `"LOAD"`.
- `C::SAMPLE_REC` returns `"EDIT"` when a sample is loaded, else `"REC."`.

`samplePath` already holds the path (`Engine.h:47`) and already survives load.
Both actions open the sample editor once D exists; until then, leave the existing
file-browser behaviour on `SAMPLE_LOAD` and make `SAMPLE_REC` a no-op when it
reads `EDIT`.

---

## 5. C — WAV metadata

Needed by SLICE `01` (FILE), by the editor's LOOP REGION row, and by SAVE.

Switch `FileBrowser::loadWavFile` from
`drwav_open_file_and_read_pcm_frames_f32` to the metadata-aware path
(`drwav_init_file_with_metadata` → read frames → walk
`pWav->pMetadata[0..metadataCount)`), and carry two new things on `SampleData`:

```cpp
    // Cue points from the WAV, used by SLICE 01 (FILE) and drawn as markers in
    // the sample editor. The M8 stores up to 128.
    static constexpr int kMaxSliceMarkers = 128;
    uint32_t sliceMarkers[kMaxSliceMarkers] = {};   // frame offsets
    int      sliceMarkerCount = 0;
    uint32_t loopStartFrame = 0;                    // from the smpl chunk
    uint32_t loopEndFrame   = 0;                    // 0/0 == no loop stored
```

`SampleData` crosses the audio-thread boundary through `LOAD_SAMPLE`, so this
must stay **trivially copyable POD** — fixed arrays, no `std::vector`, no
pointers beyond the existing sample buffer. That is an architecture invariant,
not a style preference (`ARCHITECTURE.md`).

Writing metadata back out is only needed once the editor can save (E/F); dr_wav
writes metadata through `drwav_init_file_write_sequential_pcm_frames` with a
metadata array, so plan for it but do not build it yet.

---

## 6. D — the sample editor screen

The screen does not exist. This deliverable is the screen, its navigation and
its read-only display — **no recording, no processes, no saving**. Those are E
and F. A screen that draws the waveform and moves its markers is worth having on
its own, and it is the part with no blockers.

### 6.1 The layout, measured off the device

Absolute cell coordinates; subtract 3 rows and 1 column for this codebase's
convention (as with every other screen).

```
    0000000000111111111122222222223333333333
    0123456789012345678901234567890123456789
  3 | SAMPLE                                 |
  4 |         REC   SRC VOL ARM SONG         |
  5 | RECORD  START L&R D0  20  NO           |
  6 |                                        |
  .. |          <-- waveform display -->     |
 13 |                                        |
 14 | SELECT       00000000 00001E78         |
 15 | LOOP REGION  00000000 00000000         |
 16 | SLICE MARKER 00:00000000               |
 18 | PROCESS      CROP       > UNDO         |
 20 | NAME         ALIEN-------------        |
 21 |              SAVE OVERWRITE            |
```

Exact runs: labels at col 1; the RECORD row's five values at cols 9 (`START`),
15 (`SRC`), 19 (`VOL`), 23 (`ARM`), 27 (`SONG`); the column headers on row 4 sit
above them. `SELECT` and `LOOP REGION` take two 8-hex-digit values at cols 14 and
23. `SLICE MARKER` is a single field at col 14 formatted `NN:XXXXXXXX` —
marker index, colon, frame position. `PROCESS` at col 14 with `>` at 25 and
`UNDO` at 27. `NAME` is 18 characters at col 14. `SAVE` at 14, `OVERWRITE` at 19.

**Rows 6–13 are the waveform display.** They are empty in the text grid and the
capture reports **zero rects**, both with and without a sample loaded — so the
device draws the waveform with a display primitive our decoder does not model.
That is fine: we render it ourselves from the sample data. Reserve rows 3–10 in
repo coordinates.

### 6.2 Values are frame offsets, not bytes

`SELECT` read `00000000 00001E78` for `ALIEN.WAV` — `0x1E78` = 7800, the frame
count. So the editor works in **absolute frames**, 8 hex digits, not in the
0–255 normalised units the instrument screen uses for START/LOOP ST/LENGTH.
Do not reuse the instrument screen's byte scaling here.

### 6.3 Navigation, measured with `m8drv probe`

Vertical: `RECORD → SELECT → LOOP REGION → SLICE MARKER → PROCESS → NAME → SAVE`.

**The editor opens with the cursor on `SELECT`, not on `RECORD`**, when a sample
is loaded. Worth matching, and worth knowing while scripting the device: two
enumeration attempts during this work mis-landed because they assumed the
cursor started at the top.

Horizontal on the RECORD row: `START → SRC → VOL → ARM → SONG`.

`SELECT` and `LOOP REGION` each have two values; treat them as two cursor stops
per row (start, end), as the FX cell does on PHRASE.

**`OPT` exits the editor** and discards unsaved edits with no confirmation —
confirmed by editing NAME and leaving; the instrument still referenced the
original sample and nothing was written. Match that: `OPT` pops the modal, no
dialog.

### 6.4 The editing gestures

From the manual, and they differ from every other screen — this is the part that
will feel wrong if guessed:

| Gesture | Effect |
|---|---|
| `EDIT` + `UP`/`DOWN` | large increment |
| `EDIT` + `LEFT`/`RIGHT` | small increment, **and shows a zoomed view** |
| `EDIT` + `OPT` | clear / reset the value |
| `OPT` + `UP`/`DOWN` | ±4 steps, tempo-based |
| `OPT` + `LEFT`/`RIGHT` | ±1 step, tempo-based |
| `PLAY` + `EDIT` | during playback, on a slice number or position: drop a slice marker at the playhead ("lazy chop") |

The tempo-based steps snap the marker to the song's tempo in beats. Implement
the first four; lazy chop needs the transport and belongs with F.

### 6.5 What this deliverable renders

- The waveform, downsampled to the display width, from `SampleData`.
- The SELECT range as a highlighted region.
- The LOOP REGION markers.
- Slice markers, once C lands.
- The seven rows of text fields, editable per §6.4, writing into an editor-local
  state struct — **not** into `SamplerState`. The editor edits a sample, not an
  instrument.

---

## 7. E — PROCESS actions

The list, **measured off the device** 2026-08-18 by stepping `EDIT`+`RIGHT`
from the top (§9.4). Use these strings, not the manual's:

| # | String | | # | String |
|---|---|---|---|---|
| 0 | `CROP` | | 9 | `XFADE LOOP` |
| 1 | `DELETE` | | 10 | `SQUISH(OTT)` |
| 2 | `DUPLICATE` | | 11 | `MONO:MIX` |
| 3 | `NORMALIZE` | | 12 | `MONO:LEFT` |
| 4 | `SILENCE` | | 13 | `MONO:RIGHT` |
| 5 | `REVERSE` | | 14 | `DOWNSAMPLE` |
| 6 | `INVERT` | | 15 | `8-BIT` |
| 7 | `FADE IN` | | 16 | `SLICE:AUTO` |
| 8 | `FADE OUT` | | 17 | `SLICE:SILEN` |
| | | | 18+ | `SLICE:002` … `SLICE:128` |

Two differences from the manual worth noting. It writes `SLICE:SILENC`; the
device draws `SLICE:SILEN`. It lists `16-BIT/8-BIT`; the device offered only
`8-BIT` for the 16-bit sample under test — so **`16-BIT` is conditional on the
source bit depth** and simply absent when it would be a no-op. Match that:
build the list per-sample rather than as a fixed array.

`SLICE:NNN` starts at `002`, so the manual's `SLICE:[0-128]` means 2–128.

All but three are straightforward buffer operations on the SELECT range and need
no hardware reference: CROP, DELETE, DUPLICATE, NORMALIZE, SILENCE, REVERSE,
INVERT, FADE IN/OUT, MONO, DOWNSAMPLE, 16-BIT, 8-BIT, SLICE:[0-128].

Three need thought:

- **XFADE LOOP** — crossfade shape unmeasured; equal-power is the sane default.
- **SQUISH (OTT)** — the manual says it derives COLOR and TIME from the song's
  OTT mixer settings. We have an OTT in the mixer already; reuse it rather than
  writing a second one.
- **SLICE:AUTO / SLICE:SILENC** — transient and silence detection. Thresholds are
  unmeasured. Ship something reasonable and document the constants as guesses.

**A measurement gap:** the exact on-screen strings for the PROCESS list are not
captured. Cycling PROCESS with no sample loaded does nothing (the list is inert),
and the attempt to enumerate with a sample loaded mis-landed on NAME. Take that
capture before writing the UI strings — it is one `m8drv` session.

**UNDO** sits to the right of PROCESS on the same row. One level, evidently.
Keep a single pre-process copy of the buffer.

---

## 8. F — recording

The largest piece, and the only one with an architectural prerequisite.

### 8.1 What has to exist first

`src/main.cpp:347` opens a playback stream only. Recording needs a **second SDL
audio stream** on `SDL_AUDIO_DEVICE_DEFAULT_RECORDING`, and a path from it into
a record buffer that the UI can turn into a `SampleData`. The audio-thread rules
apply unchanged: no allocation in the callback, so the record buffer is
pre-allocated by the UI and handed over, exactly as `LOAD_SAMPLE` hands a decoded
WAV the other way.

### 8.2 The RECORD row, measured

Row 4 gives the headers, row 5 the values:

| Field | Col | Default | Notes |
|---|---|---|---|
| `START` | 9 | — | `EDIT` on it starts recording. Not a value. |
| `SRC` | 15 | `L&R` | 16 values, §9.3 |
| `VOL` | 19 | `D0` | input volume |
| `ARM` | 23 | `20` | threshold to arm/start on input level |
| `SONG` | 27 | `NO` | start the song at a row while recording |

`SRC` enumerates as: `L&R, MIC, USB, INL, INR, U.L, U.R, ALL, T1, T2, T3, T4,
T5, T6, T7, T8`. Note the device draws `U.L`/`U.R` and `T1`…`T8`, where the
manual writes `U-L`/`U-R` and `TR[1-8]` — use the device's strings.

Most of those sources are meaningless on a desktop clone: there is no MIC, no
USB audio input from a host, and TR1-8 records External Instruments we do not
implement. **Implement `L&R` only**, map it to the default recording device, and
render the rest as selectable-but-inert with a note in `status.md`. Do not hide
them — a loaded project may name one.

---

## 9. Hardware evidence

Measured 2026-08-17, fw 6.5.2, COM3, via `python tools/m8drv/m8drv.py`.
Instrument 00 was switched to SAMPLER, `ST-01/ALIEN.WAV` loaded, the editor
opened, then everything reverted to HYPERSYN. **Nothing was written to the SD
card** — the editor's NAME buffer was modified during enumeration and discarded
by exiting with `OPT`, and the instrument's sample reference was still `ALIEN`
afterwards, proving no save occurred.

### 9.1 The SAMPLER instrument screen

```
 TYPE    SAMPLER       LOAD SAVE
 NAME    ------------
 TRANSP. ON   TBL.TIC 01   EQ --
 SAMPLE  LOAD             REC.
 SLICE   00OFF     AMP 00
 PLAY    00FWD     LIM 00
 START   00        PAN 80
 LOOP ST 00        DRY C0
 LENGTH  FF        MFX 00
 DETUNE  80        DEL 00
 DEGRADE 00        REV 00
 FILTER  00
 CUTOFF  FF
 RES     00
```

Note the left column runs four rows deeper than the right, and the send is
labelled `MFX` (as on every other screen).

### 9.2 The SAMPLE row is stateful

No sample: `SAMPLE  LOAD             REC.`
Loaded:    `SAMPLE  ALIEN            EDIT`

### 9.3 SRC enumeration

Stepping `EDIT`+`RIGHT` from `L&R` to the end stop gave, in order:
`L&R, MIC, USB, INL, INR, U.L, U.R, ALL, T1, T2, T3, T4, T5, T6, T7, T8` — 16
values, no wrap.

### 9.4 The PROCESS list

Enumerated by stepping `EDIT`+`RIGHT` from `CROP` with `ST-01/ALIGATOR.WAV`
loaded. 26 presses reached `SLICE:010`, with the `SLICE:NNN` run continuing;
the full list is in §7. With **no** sample loaded the field does not move at all.

### 9.5 Slice index is the raw MIDI note

`SLICE` set to `04` (drawn `04(004)` — the byte, then the resulting slice count
in parentheses), everything else default, keyjazz captures:

| Note | Peak | |
|---|---|---|
| 0 | 0.2142 | slice 0 |
| 1 | 0.1769 | slice 1 |
| 2 | 0.1615 | slice 2 |
| 3 | 0.1460 | slice 3 |
| 4, 5 | 0.0000 | past the last slice — silent |
| 12, 24, 25, 26, 36, 48, 60, 61 | 0.0000 | silent |

Control: the same instrument with `SLICE` at `00` plays note 60 at peak 0.19.
So the silence is slicing, not the instrument.

The four sounding notes are genuinely different audio — cross-correlated against
slice 0, slices 1/2/3 score 0.08 / 0.15 / 0.23.

### 9.6 START does not move a slice

Slice 0 captured with `START` at `00` and at `40`: aligned correlation
**+0.948** at a 192-sample lag, peaks 0.2142 vs 0.2100. Same region.

### 9.8 REPITCH

`ST-01/ANALOGSTRING.WAV` (8800 frames, read off the editor's `SELECT` end as
`0x2260`), PLAY `09`, keyjazz note 60, 4 s captures.

| Setup | Loop period | In beats | Confidence |
|---|---|---|---|
| STEPS `0x80`, 120 BPM | 0.2298 s | 0.460 | 0.85 |
| STEPS `0x80`, 240 BPM | 0.1152 s | 0.461 | 0.96 |
| STEPS `0x40`, 240 BPM | 0.0576 s | 0.230 | 0.97 |

Period from autocorrelating the 5 ms RMS envelope past the attack. The beats
column being constant across the first two rows is the tempo-lock; the third row
halving against the second is the STEPS proportionality.

Repitch-vs-stretch, from the two `0x80` captures: 2×-time-stretching the 240
capture and correlating against the 120 capture gives **0.918**; correlating
their raw spectra gives 0.609. Repitch.

Also confirmed: with PLAY at `09` a **`STEPS` row appears** between PLAY and
START, default `0x80`, pushing START / LOOP ST / LENGTH down one row — matching
the screen mapping `status.md` recorded from device photos in July.

### 9.7 Behaviours worth copying

- `PROCESS` does not respond at all when no sample is loaded.
- `OPT` from the editor returns to the instrument screen with no confirmation
  dialog, discarding edits.
- `SELECT`'s end value initialises to the sample's frame count (`0x1E78` for
  `ALIEN.WAV`), not to `0`.

---

## 10. Tests

Per deliverable, `[sampler]` tag, accumulate-then-assert:

- **A** — a slice test per the existing fixture: SLICE `04`, notes C-1..D#-1,
  assert each starts a quarter of the way further into the sample. A control at
  SLICE `00` asserting the old behaviour is unchanged.
- **B** — resolver-level: a sampler with an empty `samplePath` renders `LOAD` /
  `REC.`; one with `/Samples/FOO.WAV` renders `FOO` / `EDIT`.
- **C** — load a WAV with cue points, assert the markers arrive and the count is
  right; assert `SampleData` is still `std::is_trivially_copyable_v`.
- **D** — a `.m8script` that opens the editor, walks the cursor chain and asserts
  the field rows, mirroring `tests/ui/wavsynth_screen.m8script`.
- **E** — one case per process on a synthetic buffer with a known answer
  (REVERSE of a ramp is the mirrored ramp, NORMALIZE of a half-scale sine peaks
  at 1.0, and so on).
- **F** — RT safety: recording allocates nothing on the audio thread.

---

## 11. Open questions

- **Q1 — the REPITCH/BPM tempo law** — **mostly resolved** 2026-08-18 (§G,
  §9.8). REPITCH repitches, loops while held, and its loop period scales exactly
  as `STEPS / BPM`. Two things remain: the absolute constant `k` (§G.2, one
  session with a purpose-built click WAV), and whether the BPM family `0C`–`0E`
  shares the law or time-stretches (§G.3, one capture pair).
- ~~**Q2 — SLICE vs START/LENGTH**~~ — **resolved** 2026-08-18 (§3.3, §9.6):
  START is ignored; slices divide the whole file.
- ~~**Q3 — the PROCESS strings**~~ — **resolved** 2026-08-18 (§7, §9.4), and it
  turned up a conditional entry (`16-BIT`) the manual does not mention.
- **Q4 — XFADE LOOP curve, SLICE:AUTO/SILEN thresholds** (§7).

  **How to settle the slice ones — no file transfer needed.** Run `SLICE:AUTO`
  on any factory sample, then step the `SLICE MARKER` field: it displays
  `NN:XXXXXXXX`, marker index and frame position, so every marker can be read
  straight off the screen. Do the same with `SLICE:SILEN`.

  To compare against our own detection you do **not** need the original file on
  disk — capture the device playing it with `m8_capture` and run the detector on
  that recording. The capture *is* the waveform. It is noisier than the original
  and needs aligning to the marker frame numbers, but it is easily good enough to
  back out a threshold, and it costs nothing but a keyjazz capture.

  **XFADE LOOP is the one genuinely awkward item.** Hearing the result means
  saving the processed sample, which **writes to the SD card** — the only
  measurement in this document that does. Nothing in this session wrote to the
  card, deliberately. Get the owner's say-so first, save under a new name rather
  than overwriting, and only then capture the playback. Until someone wants it
  exact, equal-power is a reasonable default.
- **Q5 — the upper-pitch limit.** The manual describes a pitch ceiling enforced
  per bit depth and channel count, because the device streams from SD. We hold
  samples in RAM and have no such constraint. Almost certainly correct to ignore;
  recorded so nobody later "fixes" our sampler to match a limitation that exists
  for hardware reasons we do not share.

---

## 12. Completion checklist

- [x] Deliverable A: SLICE playback, equal divisions (2..128), note-as-index, whole-file division
- [x] Deliverable B: SAMPLE row shows base sample name and EDIT state
- [x] Deliverable C: WAV metadata: cue chunk slice markers and smpl loop region in/out
- [x] Deliverable D: Sample Editor screen layout with waveform display and cursor navigation
- [x] Deliverable E: Full PROCESS actions suite and single-level UNDO
- [ ] Deliverable F: Recording (needs SDL audio recording device input path)
- [ ] Deliverable G: PLAY `09`–`0E` (REPITCH / BPM) tempo scaling verification
- [x] Full test suite run once, counts reported
- [ ] Q1, Q4, Q5 recorded as unmeasured open items

