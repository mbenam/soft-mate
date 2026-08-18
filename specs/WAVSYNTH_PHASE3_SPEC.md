# WavSynth Phase 3 — the 61 built-in wave tables

**Status:** specification. The **data and tooling already exist in the tree**
(§2, §3); what remains is the engine and UI work in §4–§6.
**Depends on:** `WAVSYNTH_PHASE2_SPEC.md` must be implemented first. Phase 3 adds
one branch to the table generator Phase 2 builds, and widens one clamp in the UI.
Do not start this before Phase 2's `regenerateWavTable()` exists.
**Hardware evidence:** measured on a real M8 (fw 6.5.2, COM3) on 2026-08-17.
Everything in §4 is measured, not inferred. See §7.

---

## 1. What this phase does

Phase 2 left shapes `0x09`–`0x45` aliasing to a sine. This phase makes them play
the real wave tables: 61 tables of 64 frames each, with SCAN morphing between
frames.

The wave table data is **digitised from the manual's own Wave Table Index**.
`manual/wavesynth.pdf` draws every frame of every table as a **vector polyline**,
not a bitmap, which makes the index a machine-readable source. A capture from
real hardware matches the digitised frame at **r = 0.998** (§7.1), so this is not
an approximation of the character of the tables — it is the tables.

**In scope:** the wave table branch of the table generator, SCAN as a morph, the
generated data bank, tests, and widening the UI's SHAPE clamp.

**Out of scope:** anything Phase 2 put out of scope stays out (band-limiting,
LIM `06`–`08`, instrument FX commands, `ModDest` additions).

---

## 2. Where the data comes from

`tools/wavetables/extract_manual_wavetables.py` reads the PDF and writes the
bank. It is already written and already run; you do not need to run it unless
the extraction changes. If you do:

```bash
python tools/wavetables/extract_manual_wavetables.py --pdf manual/wavesynth.pdf --out src/engine/data/WavetableBank.cpp
```

It needs `pymupdf` and `numpy`.

**`manual/` is gitignored** (`.gitignore:16`), so a clean clone does not have the
PDF and cannot re-run the extractor. That is why the generated
`src/engine/data/WavetableBank.cpp` is committed rather than built. Do not delete
it expecting to regenerate it, and do not add a build step that tries to.

The geometry the tool recovers (and why each constant is read from the document
rather than assumed) is documented in its docstring; the short version:

- 8 tables per page, 64 frames per table, 61 tables over 8 pages.
- Each frame is a polyline sampled at exactly **200 x positions** on a ~0.141 pt
  lattice. That is the resolution ceiling of this source — the underlying buffer
  is finer (the plots change value at nearly every x position), so the bank
  stores 200 samples per frame and invents nothing beyond that.
- Row pitch and origin differ per page (7.6089 / 7.6818 / 7.6855 pt) and are read
  from the hex row labels. Assuming one pitch for the whole document drifts by
  half a row by frame 63 — that was a real bug during extraction.
- The vertical baseline and full-scale half-span are recovered per page from the
  envelope of every stroke. They come out at ~3.487 pt on all eight pages, which
  is the cross-check that the pages share one vertical scale.

**Table order is verified.** The 61 header strings in the PDF match, name for
name and in order, the 61 wave table names read off the device screen in
Phase 2's Appendix B — 61/61, no exceptions. Bank index `i` is shape
`0x09 + i`: index 0 is `OSC:CRUSH` (shape `0x09`), index 60 is `VOX:VOXSYNTH`
(shape `0x45`).

---

## 3. The data as it ships

`src/engine/data/WavetableBank.cpp` — **already generated and in the tree**,
2.79 MB of source, 4101 lines, 780,800 `int8_t` samples.

It is a generated C++ translation unit rather than a data file on disk because
the engine must not depend on the working directory: `m8_tests`, `m8_render` and
`m8_clone` all run from different places, and `m8_engine` links no file-system
helpers. Compile cost was measured before choosing this: **3.7 s** with MSVC
`/O2 /std:c++20`, producing a 791 KB object. That is acceptable and it only
recompiles when the generated file changes, which is never in normal work.

Write the matching header yourself — the generated `.cpp` includes it:

```cpp
// src/engine/data/WavetableBank.h
#pragma once
#include <cstdint>

namespace m8::engine {

// The 61 built-in wave tables, digitised from the manual's Wave Table Index
// (WAVSYNTH_PHASE3_SPEC.md). Bank index i is WavSynth shape 0x09 + i.
inline constexpr int kWavetableCount  = 61;
inline constexpr int kWavetableFrames = 64;
inline constexpr int kWavetableLength = 200;   // the manual's plot resolution

extern const char* const kWavetableNames[kWavetableCount];
extern const int8_t kWavetableData[kWavetableCount]
                                  [kWavetableFrames][kWavetableLength];

} // namespace m8::engine
```

Add `src/engine/data/WavetableBank.cpp` to the `m8_engine` target in
`CMakeLists.txt`, alongside the other `src/engine/*` sources. It links into
`m8_engine`, so `m8_tests` and `m8_render` get it for free and no SDL dependency
is introduced.

`kWavetableNames` is not used by the engine — the UI already has its own name
table from Phase 2 §5.3. It is there so a test can assert the two agree (§6.2).

---

## 4. Engine — the wave table branch

All of this goes inside Phase 2's `regenerateWavTable()`. Nothing else in the
voice changes: the table is still generated once per parameter change, still
played by the same phase accumulator, still passes through the same output
stage. A wave table shape costs exactly what a base shape costs at play time.

### 4.1 What SCAN means changes, and this is the part that is easy to get wrong

On base shapes `00`–`08`, SCAN is the **mirror** position (Phase 2 §3.5). On
wave table shapes `09`–`45`, SCAN is the **morph** — it scans through the 64
frames — and there is no mirroring at all.

So the mirror stage must be **skipped** for wave table shapes. If you leave
Phase 2's `wavMirrorPhase()` in the path for shape ≥ 9, every wave table will be
folded back on itself as SCAN rises, which will look plausible and sound wrong.

### 4.2 Frame selection and the crossfade

Measured on hardware (§7.2): SCAN linearly crossfades between adjacent frames.
It does not step.

```cpp
// SCAN spans the 64 frames over the full byte range. The manual's own row
// labels in the Wave Table Index are exactly floor(i * 255 / 63) for frame i,
// which is this mapping read the other way round.
const float pos = std::clamp(ws.scan, 0, 255) * (kWavetableFrames - 1) / 255.0f;
const int   f0  = static_cast<int>(pos);
const int   f1  = std::min(f0 + 1, kWavetableFrames - 1);
const float mix = pos - static_cast<float>(f0);
```

### 4.3 Reading a frame

Frames are 200 samples; the table being generated is `len` samples (Phase 2:
`len = clamp(SIZE, 4, 255)`). Read the frame with linear interpolation and wrap
at the loop point:

```cpp
// One sample of wave table `wt`, frame `f`, at position u in [0,1).
static float wtFrameSample(int wt, int f, float u) {
    const float idx = u * kWavetableLength;
    int i = static_cast<int>(idx);
    if (i >= kWavetableLength) i = kWavetableLength - 1;
    const int j = (i + 1) % kWavetableLength;      // wraps: frames are cycles
    const float frac = idx - static_cast<float>(i);
    const float a = kWavetableData[wt][f][i] * (1.0f / 127.0f);
    const float b = kWavetableData[wt][f][j] * (1.0f / 127.0f);
    return a + (b - a) * frac;
}
```

### 4.4 The generation loop

Replace Phase 2's single generation loop with a branch. Everything except the
source of the sample — WARP, MULT, the `WAV` filter modes, the guard sample, the
cache key — is unchanged.

```cpp
    const bool isWt = (ws.shape >= 9 && ws.shape < 9 + kWavetableCount);
    const int  wt   = isWt ? (ws.shape - 9) : 0;
    const int  shape = isWt ? 0 : std::clamp(ws.shape, 0, 8);

    // Wave table shapes use SCAN as the frame morph, so there is no mirror.
    const float mirror = isWt ? 0.0f
                              : std::clamp(ws.scan, 0, 255) / 255.0f * 2.0f;

    float pos = 0.0f; int f0 = 0, f1 = 0; float mix = 0.0f;
    if (isWt) {
        pos = std::clamp(ws.scan, 0, 255) * (kWavetableFrames - 1) / 255.0f;
        f0  = static_cast<int>(pos);
        f1  = std::min(f0 + 1, kWavetableFrames - 1);
        mix = pos - static_cast<float>(f0);
    }

    for (int i = 0; i < len; ++i) {
        float u = static_cast<float>(i) / static_cast<float>(len);
        u = wavMirrorPhase(u, mirror);          // SCAN, base shapes only
        u = wavWarpPhase(u, warp01);            // WARP
        u = u * static_cast<float>(repeats);    // MULT
        u = u - std::floor(u);

        float v;
        if (isWt) {
            v = wtFrameSample(wt, f0, u) * (1.0f - mix)
              + wtFrameSample(wt, f1, u) * mix;
        } else {
            v = wavBaseShape(shape, u);
        }
        m_wavTable[i] = std::clamp(v, -1.0f, 1.0f);
    }
```

`wavMirrorPhase(u, 0.0f)` returns `u` unchanged, so the base-shape path is
byte-for-byte what Phase 2 produced. Shapes above `0x45` cannot occur — the file
format's enum stops there — but `isWt` is written as a range test anyway so a
corrupt byte falls back to `PULSE 12%` instead of reading off the end of the
bank.

### 4.5 What SIZE does to a wave table

Nothing new. Measured on hardware (§7.3): SIZE is the table length for wave
table shapes exactly as it is for base shapes. At SIZE `FF` a high-frequency
frame comes back whole; at SIZE `20` the same frame comes back decimated to
around 32 samples, with its high-frequency energy collapsed from 0.945 to 0.257
of total. The pitch does not change either way.

The loop above already does this — it evaluates the frame at `len` positions —
so there is nothing to add. It is written down because "SIZE decimates the wave
table" is a claim someone will want the evidence for.

### 4.6 The cache key needs nothing new

Phase 2's key already contains `shape`, `size`, `mult`, `warp`, `scan`,
`filter_type`, `cutoff` and `res`. Frame selection derives entirely from `shape`
and `scan`, both already in the key. Sweeping SCAN from the UI regenerates the
table on each step, which is a user-gesture rate, not an audio rate.

---

## 5. UI

One change. In `HandleInstrumentInput` (Phase 2 §5.5), the SHAPE edit branch is
clamped to `0, 8` with a `// TODO(phase3)` on it. Widen it:

```cpp
    else if (isWav && cursor_id == C::SHAPE)
        PushParam(commandSink, uiEngineState, ParamID::WAV_SHAPE,
                  std::clamp<int>(inst.wav.shape + step, 0, 0x45), currentInstIndex);
```

That is all. The 70-entry `WavShapeName()` table and the accent rendering went in
during Phase 2 precisely so this phase would not touch the UI again. Remove the
`TODO(phase3)` comment.

---

## 6. Tests

### 6.1 Everything from Phase 2 must still pass

`[wavsynth]` must stay green, unchanged. The base-shape path is untouched by
design (§4.4) — if a base-shape test moves, the branch is wrong.

### 6.2 New cases, `tests/test_wavsynth.cpp`, `[wavsynth]` tag

Accumulate a flag and assert once; never assert inside a per-sample loop.

1. **`Wavetable bank is well formed`** — every one of the 61 × 64 frames has at
   least one non-zero sample except the known-flat ones, no sample exceeds
   ±127, and `kWavetableNames[i]` equals the UI's `WavShapeName(0x09 + i)` for
   all 61. That last assertion is the one that catches a bank regenerated in a
   different order.
2. **`Wavetable shapes render and differ from each other`** — render shapes
   `0x09`, `0x1F`, `0x2A`, `0x45` at the same note and require all four outputs
   finite, non-silent, and pairwise different.
3. **`SCAN crossfades between adjacent frames`** — this is the behavioural test
   and it has an exact fixture. `BNK:SCRATCH` (shape `0x1F`) frames 2 and 3 are
   near-exact phase inversions of one another. Render at SCAN `0x08` (frame 2),
   SCAN `0x0C` (frame 3) and SCAN `0x0A` (the midpoint) and require the midpoint
   peak to be at least 10× smaller than either endpoint. If SCAN stepped instead
   of crossfading, the midpoint would be full amplitude and this test fails.
   Hardware gives 0.428 / 0.027 / 0.407 for exactly this (§7.2).
4. **`SCAN 0x00 selects frame 0 and 0xFF selects frame 63`** — render both and
   correlate against the bank rows directly, or more simply assert that SCAN
   `0x00` output differs from SCAN `0xFF` output for a table whose first and last
   frames differ.
5. **`SIZE decimates a wavetable frame`** — render `BNK:SCRATCH` at SCAN `0x55`
   (frame 21, the high-frequency one) with SIZE `0xFF` and SIZE `0x20`, and
   require the high-frequency energy of the second to be markedly lower. Compute
   it as the ratio of summed |first difference| to peak — no FFT needed.
6. **`Wavetable rendering allocates nothing`** — extend the existing RT-safety
   case to a wave table shape with a SCAN change mid-render.

### 6.3 No new UI script

Phase 2's `tests/ui/wavsynth_screen.m8script` already covers the screen. Widening
a clamp does not need its own script.

---

## 7. Hardware verification — what was measured, and how

All captures: real M8, fw 6.5.2, COM3, USB audio, 48 kHz, note MIDI 36
(65.4 Hz ≈ 734 samples per cycle, so one cycle comfortably oversamples a
200-sample frame). Device set to WAVSYNTH with everything neutral: SIZE `FF`,
MULT `00`, WARP `00`, FILTER `00`, AMP `00`, LIM `00`, PAN `80`, DRY `C0`, sends
`00`. Instrument 00 was restored to HYPERSYN afterwards; nothing was written to
the SD card.

Reproduce any of it with:

```bash
build/Release/m8_capture.exe --port COM3 --audio "M8" --keyjazz 36 --seconds 2 --out cap.wav
```

```bash
python tools/wavetables/compare_capture.py cap.wav OSC:GRAPHIC --bank wavetables.bin --scan 0x00
```

(`wavetables.bin` is the optional flat-binary output of the extractor: pass
`--bin wavetables.bin` when running it. The engine does not use that file.)

### 7.1 The digitised frames are the real frames

`OSC:GRAPHIC` (shape `0x0E`), SCAN `0x00`, 77 cycles averaged:

| Compared against | Correlation |
|---|---|
| extracted frame 0 | **0.998** |
| extracted frame 1 | 0.994 |
| extracted frame 3 | 0.994 |
| best of all other 61 frames | 0.953 |

Frame 0 is both an excellent match and the best match of all 64 candidates. The
extraction pipeline is correct end to end — geometry, baseline, scale, ordering.

### 7.2 SCAN crossfades; it does not step

The decisive fixture: in `BNK:SCRATCH` (shape `0x1F`), frames 2 and 3 are
near-exact phase inversions (their sum peaks at 0.081 of full scale). A
crossfade halfway between them must cancel; a frame selector cannot.

| SCAN | Frame(s) | Captured peak |
|---|---|---|
| `0x08` | frame 2 | 0.428 |
| `0x0A` | midway between 2 and 3 | **0.027** |
| `0x0C` | frame 3 | 0.407 |

A 24 dB collapse at the midpoint. That proves three things at once: SCAN
crossfades linearly, the crossfade happens in the wave table (not at the output),
and our frames 2 and 3 are genuinely the frames the device plays at those SCAN
values — otherwise they would not cancel.

The residual 0.027 is consistent with the 0.081 imperfection in the frame pair
itself, so it does not indicate an offset in the frame mapping.

A softer confirmation at the other end of the range: SCAN `0x80` best-matched
frame 32 (0.973), where `0x80 * 63 / 255 = 31.6`.

### 7.3 SIZE decimates the wave table

`BNK:SCRATCH`, SCAN `0x55` (frame 21 — the most high-frequency frame in that
table, 0.851 of its energy above the 25th harmonic):

| SIZE | vs the full 200-sample frame | vs the frame decimated to 16–32 | HF energy fraction |
|---|---|---|---|
| `0xFF` | **0.954** | ~0.02 | 0.945 |
| `0x20` | 0.055 | **0.78** | 0.257 |

At SIZE `FF` the device plays the frame whole; at SIZE `0x20` it plays roughly 32
samples of it. Pitch was unchanged in both (measured period 734 samples both
times). This is the same SIZE semantics Phase 2 adopted for base shapes, so
Phase 2 open question **O1 is now answered** — update that spec's §8 when you
touch it.

### 7.4 Two driver notes

Named-field navigation does not work on the WavSynth instrument screen
(`ScreenModel` knows only the SAMPLER and MACROSYN variants), so all of the above
was driven with raw `PRESS` keys through `m8drv batch`. Changing instrument TYPE
after editing parameters raises a `LOSE CHANGES TO INSTRUMENT?` modal that needs
**two** EDIT presses. Both are recorded in `WAVSYNTH_PHASE2_SPEC.md` Appendix A.5.

---

## 8. Open questions — record, do not guess

- **P1 — WARP and MULT on wave table shapes.** §4.4 applies both to wave tables
  exactly as to base shapes, which is what the manual implies (it does not
  restrict them). Every hardware capture in §7 was taken at WARP `00` and MULT
  `00`, so the interaction is untested. If it turns out the M8 bypasses one of
  them for wave tables, this is a two-line change.
- **P2 — resolution ceiling.** The bank is 200 samples per frame because that is
  what the manual plots. The device's own buffer is finer. For a frame with
  strong high-frequency content this is a real, if small, loss; the §7.1 match at
  0.998 bounds how much it costs.
- **P3 — DC offset.** Frames are stored exactly as plotted, including their DC
  offset; 29% of frames have a mean beyond ±0.05 of full scale, and a few are
  nearly pure DC. This is faithful to the source, and no hardware measurement has
  contradicted it, but nothing has confirmed that the device does not remove DC
  somewhere downstream either.
- **P4 — polarity.** §7.1's match is positive, so the bank's sign convention is
  right. One later comparison (`BNK:SCRATCH` frame 21) matched best inverted, but
  that frame is nearly odd-symmetric, where a half-period circular shift is
  indistinguishable from inversion — so it is degeneracy in the measurement, not
  evidence of a sign error. Worth one clean re-check on an asymmetric frame if
  anything downstream ever looks phase-flipped.

---

## 9. Completion checklist

- [ ] `src/engine/data/WavetableBank.h` written (§3)
- [ ] `src/engine/data/WavetableBank.cpp` added to the `m8_engine` target
- [ ] §4.1 mirror stage gated off for shape ≥ 9 — the easy-to-miss one
- [ ] §4.2 SCAN crossfade, §4.3 frame read, §4.4 generation branch
- [ ] §5 SHAPE clamp widened to `0x45`, `TODO(phase3)` removed
- [ ] §6.2 six new cases; all Phase 2 `[wavsynth]` cases still green unchanged
- [ ] Full suite run once, counts reported
- [ ] Phase 2 §8 open question O1 marked answered by §7.3
- [ ] P1–P4 restated in the completion report as still unmeasured
- [ ] `status.md` WavSynth entry updated — it currently says shapes 9+ alias to
      sine, which this phase makes false
