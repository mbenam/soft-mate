# Sampler completion + Sample Editor

**Status:** specification. Nothing here is implemented yet.
**Supersedes:** `M8_SAMPLER_COMPLETION_SPEC.md` Phases 2 and 3, which sketched this
work in a paragraph each. Phase 1 of that document is **done** (2026-07-17) and
Phase 4 is **mostly done** (ZDF LP/HP and LIM POST/POST:AD landed). Keep that
file for its Phase 1 record; this one replaces its forward-looking half.
**Hardware evidence:** measured on a real M8 (fw 6.5.2, COM3) on 2026-08-17.
Every screen coordinate, enum and cursor chain in §6 was read off the device.

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

| # | Deliverable | Size | Blocked? |
|---|---|---|---|
| A | SLICE playback, equal divisions | small | no |
| B | SAMPLE row shows the sample name + EDIT | small | no |
| C | WAV metadata: cue chunk in, loop region in/out | medium | no |
| D | The sample editor screen (no recording, no processes) | large | no |
| E | PROCESS actions | large | partly — see §7 |
| F | Recording | large | needs an audio input path |

**PLAY `09`–`0E` is deliberately not on this list.** It is blocked on a
measurement nobody has taken (§8.1), and guessing the tempo law would violate
`AGENTS.md` §4. Take the measurement first, then spec it.

---

## 3. A — SLICE playback

### 3.1 The encoding, hardware-verified

Read off the device screen 2026-07-17 (recorded in `status.md`):

| Byte | Meaning |
|---|---|
| `00` | OFF |
| `01` | FILE — use the markers embedded in the WAV (needs C) |
| `02`–`0x80` | 2–128 equal divisions; **the byte value is the slice count** |

Slices map to notes from **C-1 = MIDI 24** upward: slice index `= note - 24`.
(C-4 = 60 is the sampler root, and C-1 is three octaves below.)

### 3.2 What to implement

Only the equal-division case (`02`–`0x80`). `01` (FILE) depends on deliverable C
and is a separate change.

In `SamplerEngine::computeRegion`, when `s.slice >= 2`:

```cpp
    const int count = std::clamp(s.slice, 2, 128);
    const int idx   = std::clamp(noteMidi - 24, 0, count - 1);   // C-1 upward
    const int32_t sliceLen = frames / count;
    m_startFrame = idx * sliceLen;
    m_loopStart  = m_startFrame;
    m_loopEnd    = m_startFrame + sliceLen;
```

`computeRegion` does not currently receive the note. It is called from
`SamplerEngine::noteOn` and from `SynthVoice::noteOn`; both have the frequency
but not the MIDI note. **Pass the MIDI note down** rather than deriving it back
from frequency — a derived note is wrong the moment DETUNE or a table transpose
is in play, and slice choice must follow the written note, not the sounding
pitch.

### 3.3 The one thing to confirm on hardware first

How SLICE interacts with `START` and `LENGTH` is **not** measured. Two readings
are plausible: slices divide the whole file and ignore START/LENGTH, or they
divide the START..START+LENGTH region. Settle it with one capture before
writing the code — set SLICE to `04`, START to `40`, play C-1 and C-2, and see
whether the onsets move.

Until then, implement "slices divide the whole file", which is the simpler
reading and matches how the manual describes it ("slices the sample into equal
length sections"), and leave a comment saying it is unconfirmed.

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

Sixteen actions from the manual: CROP, DELETE, DUPLICATE, NORMALIZE, SILENCE,
REVERSE, INVERT, FADE IN, FADE OUT, XFADE LOOP, SQUISH (OTT), MONO:MIX/LEFT/RIGHT,
DOWNSAMPLE, 16-BIT, 8-BIT, SLICE:AUTO, SLICE:SILENC, SLICE:[0-128].

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

### 9.4 Behaviours worth copying

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

- **Q1 — the REPITCH/BPM tempo law.** PLAY `09`–`0E` cannot be written without
  it: how STEPS maps to a count, whether REPITCH repitches while BPM
  time-stretches, and what the ratio law is. `status.md` has the screen mapping
  already (REPITCH exposes STEPS, BPM exposes BPM, in the row under PLAY,
  default `0x80`) but not the audio behaviour. One capture session with a
  known-length loop at two tempi settles it.
- **Q2 — SLICE vs START/LENGTH** (§3.3).
- **Q3 — the PROCESS strings** (§7).
- **Q4 — XFADE LOOP curve, SLICE:AUTO/SILENC thresholds** (§7).
- **Q5 — the upper-pitch limit.** The manual describes a pitch ceiling enforced
  per bit depth and channel count, because the device streams from SD. We hold
  samples in RAM and have no such constraint. Almost certainly correct to ignore;
  recorded so nobody later "fixes" our sampler to match a limitation that exists
  for hardware reasons we do not share.
