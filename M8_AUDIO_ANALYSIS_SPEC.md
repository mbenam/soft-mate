# M8-SDL3 — Audio Analysis Tools Spec

Two C++ tools so the agent can measure its own output instead of shipping a WAV to a human and
waiting. Both link `m8_engine` only — no SDL.

- **`m8_analyze`** — objective, single-file checks. No reference needed. Pass/fail numbers the
  agent can act on, and the same checks wrapped as `[audio]` regression tests.
- **`m8_spectrum`** — A/B spectral comparison of two WAVs against a hardware reference. The
  tool for building the synths to parity.

**Why C++ and not the Python we used:** the agent runs the whole loop itself — render, measure,
read the number, fix, repeat — with no round-trip through a human. The objective checks also
become tests, so the regressions we already fixed (DC drift, no headroom, the 45×-too-strong
pitch mod) can never come back silently.

**Prerequisite:** none. `m8_render` and the `EngineEvent` ring already exist.

---

## Part A — `m8_analyze`

### What it does

Takes a rendered WAV (and optionally its events CSV), prints objective measurements, and exits
non-zero if any hard check fails. No reference signal — these are properties a correct render
must have regardless of what the song is.

```
m8_analyze song.wav
m8_analyze song.wav --events song_events.csv
m8_analyze song.wav --json report.json
```

### Measurements

**Level / dynamics** (whole file, and per 1-second window):
- peak (max |sample|)
- RMS (sqrt of mean square)
- crest factor dB = `20·log10(peak/rms)`
- clipped-sample count (|s| ≥ 0.999)

**Health:**
- DC offset = mean(sample), per channel. Overall and per-second (to catch drift that averages
  to zero over the whole file — that is how the feedback-integrator DC hid).
- non-finite count (NaN / Inf)
- longest silent gap (RMS below −60 dB for how long) — catches stuck/dropped voices

**Stereo:**
- L/R correlation, mid RMS, side RMS (is the stereo field doing anything)

**Per-note, if `--events` given** — for each `NOTE_ON`, analyse the audio window from that
sample to the next note-on on the same track:
- measured pitch (FFT peak in a band around the expected `frequency`) vs nominal, in cents
- spectral centroid at note start vs note end (does the filter/brightness move)
- attack time (samples from note-on to peak envelope)

### Hard checks (non-zero exit if violated)

```
peak        < 1.0            (no digital clipping)
clipped     == 0
non-finite  == 0
|DC|        < 0.005  overall, AND < 0.01 in any 1-second window
crest       > 6 dB          (not slammed flat into the limiter)
```

These are deliberately loose — they catch gross breakage, not taste. Pitch/centroid are
**reported**, not asserted, unless `--events` plus `--expect` flags are given (see tests).

### Output

Human-readable table to stdout; `--json` writes a machine-readable version the agent can parse.
Exit code 0 = all hard checks pass, 1 = a hard check failed, 2 = bad input.

### Implementation notes

- WAV reader: reuse the writer from `m8_render` in reverse, or a 30-line RIFF parser. 16-bit
  PCM stereo is all you need to read.
- The FFT: see Part C. `m8_analyze` needs it for pitch and centroid.
- **Reuse `OfflineHost` is NOT needed here** — this tool reads a file, it doesn't render.
- Keep the per-note analysis behind `--events`; without the CSV, just do the file-level checks.

### Acceptance

- `m8_analyze` on the current demo render: DC < 0.001, crest ~10 dB, 0 clipped, exit 0.
- Feed it a deliberately broken WAV (add 0.2 DC, or a NaN) → non-zero exit, the failure named.

---

## Part B — the `[audio]` regression tests

Wrap the hard checks as tests so they run in the suite, not just on demand. These render
through `OfflineHost` (in-process, no file) and run the same measurement code as `m8_analyze` —
factor the measurement into a shared `src/analysis/AudioMetrics.{h,cpp}` that both the tool and
the tests call.

```cpp
// src/analysis/AudioMetrics.h — pure, no I/O, no SDL
struct Metrics {
    float peak, rms, crestDb, dcL, dcR, dcWorstWindow;
    int   clipped, nonFinite;
    float longestSilenceSec;
};
Metrics analyze(const float* interleavedStereo, size_t frames, int sampleRate);

float pitchHz(const float* mono, size_t n, int sr, float expectedHz);   // FFT peak in band
float spectralCentroidHz(const float* mono, size_t n, int sr);
```

Tests, tagged `[audio]`:

| # | Test | Assert |
|---|---|---|
| A1 | demo song, 40 s | `dcWorstWindow < 0.01`, `crestDb > 9`, `clipped == 0`, `nonFinite == 0` |
| A2 | demo song | longest silence < 0.5 s (no dropped section) |
| A3 | single sampler note, LFO→PITCH amt 0xFF | measured pitch within 1 semitone of nominal (this is the ±280-cent bug, pinned) |
| A4 | single note, AHD→CUTOFF | centroid at note end < centroid at note start (filter closes) |
| A5 | reverb+delay feedback at 0xFF, 30 s loud input | no divergence: peak stays < 1.0, DC < 0.005 |

A3 and A5 are the ones that would have caught real bugs we hit. They belong in CI.

---

## Part C — the FFT

Do not hand-roll it. Vendor a single-file public-domain FFT into `third_party/`:

- **kissfft** (`kiss_fft.c` + `kiss_fftr.c` for real input) — public domain, ~1 file, no deps.
- or **PFFFT** — faster, also single-file.

kissfft is the safe choice. Add `kiss_fftr.c` to `m8_engine` (or a small `m8_analysis` lib),
wrap it once:

```cpp
// src/analysis/Fft.h
// Real FFT of `n` samples (n a power of two, or kissfft's mixed-radix). Returns magnitude
// spectrum of length n/2+1. Applies a Hann window internally.
std::vector<float> magnitudeSpectrum(const float* mono, size_t n);
```

`pitchHz` = bin of the peak magnitude within `[expected·0.85, expected·1.18]`, converted to Hz.
`spectralCentroidHz` = `Σ(f·mag) / Σ(mag)`.

**Window matters.** Always apply a Hann window before the FFT or the leakage will smear the
centroid and misplace the pitch peak. Bake it into `magnitudeSpectrum` so no caller forgets.

---

## Part D — `m8_spectrum` (synth parity)

The A/B tool. Given two WAVs — yours and a hardware reference — it reports where the spectra
diverge. This is how you build Macrosyn, FM, etc. to parity, because "does it sound like the
M8" is a comparison, not a boolean.

```
m8_spectrum --ref m8_capture.wav --test my_render.wav
m8_spectrum --ref ref.wav --test mine.wav --align --json diff.json
```

### What it does

1. **Align.** The two recordings won't start at the same sample. Cross-correlate the onsets (or
   the first N ms) and shift `test` to match `ref`. `--no-align` to skip if you trimmed both.
2. **Windowed spectra.** FFT both over the sustained portion of the note (skip the attack
   transient — the first ~50 ms — which is where they'll differ most and matter least for
   identifying the oscillator).
3. **Compare.** Report:
   - fundamental of each (should match — if not, pitch is wrong, stop here)
   - **harmonic/sideband table**: for each significant peak in `ref`, the frequency and the
     amplitude in dB in both, and the delta. This is the money output. For FM, the sidebands
     sit at `carrier ± n·mod`; if yours are at the right frequencies but the wrong amplitudes,
     your modulation index is off. If they're at the wrong frequencies, your operator ratio is
     off.
   - spectral centroid of each (gross brightness match)
   - a single scalar **log-spectral distance** (mean |dB difference| across bins) so the agent
     has one number to minimise as it tunes.

### Output

```
fundamental   ref 261.6 Hz   test 261.6 Hz   OK
harmonics:
  freq(Hz)   ref(dB)  test(dB)   delta
    261.6     -6.0     -6.1      -0.1
    523.3    -12.4    -18.9      -6.5   <-- test 6.5 dB low
    784.9    -20.1    -14.2      +5.9   <-- test 5.9 dB high
    ...
centroid      ref 1840 Hz   test 2210 Hz   (+370, test brighter)
log-spectral distance: 4.2 dB
```

That table tells the agent exactly what to change: which partials are too loud or too quiet.

### Implementation notes

- Reuse `magnitudeSpectrum` from Part C.
- Peak-picking: find local maxima above a floor (e.g. −60 dB from the max), merge bins within a
  few Hz.
- `--json` emits the harmonic table and the scalar distance so the agent can loop on it
  programmatically: render → compare → read distance → adjust → repeat.
- This tool does **not** render — it compares two files. The reference comes from hardware; the
  test comes from `m8_render`.

### Acceptance

- `m8_spectrum --ref x.wav --test x.wav` (same file) → log-spectral distance ≈ 0, all deltas 0.
- Feed it a sine vs the same sine one octave up → fundamentals differ, flagged.

---

## Part E — capturing the hardware reference

`m8_spectrum` needs a clean single-note recording off real hardware. That is its own tool set,
specified separately in **`M8_CAPTURE_SPEC.md`** (`m8_makeprobe` + `m8_capture`, both C++).

Build those when you start Macrosyn — nothing needs A/B parity before then. `m8_analyze` and
the `[audio]` tests (Parts A/B) pay off immediately and do not depend on any of it.


## Order

1. **Part C** — vendor kissfft, wrap `magnitudeSpectrum`.
2. **Part A + B** — `AudioMetrics`, `m8_analyze`, the `[audio]` tests. Immediate value: run
   `m8_analyze` on the demo and every future render; A3/A5 go into CI.
3. **Part D** — `m8_spectrum`. Build it when you start Macrosyn.
4. **Part E** — the capture rig. Same time as D.

CMake, per `AGENTS.md`:

```cmake
add_executable(m8_analyze  src/tools/main_analyze.cpp)
add_executable(m8_spectrum src/tools/main_spectrum.cpp)
target_link_libraries(m8_analyze  PRIVATE m8_engine)   # m8_engine now includes m8_analysis + kissfft
target_link_libraries(m8_spectrum PRIVATE m8_engine)
```

Both link `m8_engine` only. If either pulls in SDL, that's a bug.
