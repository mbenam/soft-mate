# WavSynth Phase 2 — UI, DSP rebuild, 9 base shapes

**Status:** specification, not yet implemented.
**Supersedes:** the DSP sections of `WAVSYNTH_IMPLEMENTATION.md` (Phase 1). That
document stays in the tree as the record of what Phase 1 built; its §3
(`generateWavShape`, the render path) is replaced wholesale by §3 here.
**Hardware evidence:** measured on a real M8 (fw 6.5.2, COM3) on 2026-08-17.
See Appendix A. Every screen coordinate, enum name and default in this spec was
read off the device, not inferred from the manual.

---

## 0. Scope

**In scope (this phase):**

1. Rebuild the WavSynth DSP: fix the per-sample regeneration, fix the loop-point
   interpolation, correct WARP/SIZE semantics, make the 9 base shapes right.
2. Build the WavSynth instrument screen — it does not exist today. WavSynth is
   currently unreachable from the app; it can only enter the engine by loading a
   `.m8s` that already contains one.
3. Correct the WavSynth defaults to what the hardware actually initialises.

**Out of scope (Phase 3, separate spec):** the 61 built-in wave tables
(shapes `0x09`–`0x45`) and SCAN-as-morph. This phase keeps the existing
behaviour for shapes ≥ 9 — they alias to the sine table — but the UI **does**
display their real names (Appendix B), because a loaded song may contain one and
showing `SINE` for `WT-EFX:CYBERNET` would be a lie on screen.

**Explicitly not in scope, do not do:** anti-aliasing / band-limiting of the base
shapes; LIM modes `06`–`08` (POST:W1–W3), which fall back to hard clip for every
synth in this codebase, not just WavSynth; instrument-level FX commands; adding
shape/size/warp/scan to `ModDest`; touching the Sampler or Macrosyn layouts.

---

## 1. What exists today, and what is wrong with it

Read these before writing any code:

| Thing | Where | State |
|---|---|---|
| `WavSynthState` | `src/engine/Engine.h:94` | Complete. Two defaults are wrong (§2). |
| `generateWavShape()` | `src/engine/SynthVoice.cpp:99` | **Replaced entirely** by §3. |
| `readWavBuf()` | `src/engine/SynthVoice.cpp:161` | **Replaced** by §3.6. Has a wrap bug. |
| WavSynth render block | `src/engine/SynthVoice.cpp:583` | **Replaced** by §3.7. |
| Output amp/lim/filter tail | `src/engine/SynthVoice.cpp:648` | Keep as is, unchanged. |
| `ParamID::WAV_*` | `src/engine/CommandRing.h:62` | Complete, no change. |
| Param routing | `src/engine/EngineStateUpdater.h:184` | Complete, no change. |
| `.m8s` load/save | `src/io/SongIO.cpp:922,1164,1266` | Complete. Only new-song defaults change (§4). |
| Instrument screen | `src/ui/screens/instrument/` | Dispatches four ways (Sampler / Macrosyn / FMSynth / HyperSynth). **WavSynth is the one type with no layout.** §5. |
| Tests | `tests/test_wavsynth.cpp` | 5 cases, all must still pass. Extended in §6. |

### 1.1 The three defects being fixed

**D1 — `generateWavShape()` runs once per output sample.**
[`SynthVoice.cpp:591`](../src/engine/SynthVoice.cpp:591) calls it from inside
`renderSample()`, which is per-sample. At the current default SIZE it rewrites
1024 floats (with a `sinf` each, for shape 6) for **every single output sample** —
roughly 50 M operations/second per sounding voice. With FILTER `08`–`0B` it also
runs the SVF over the whole buffer per output sample. It is correct, just
absurdly expensive, and it will not survive Phase 3's wavetable reads.
Fix: generate into a cached table, regenerate only when the parameters change.

**D2 — the loop point interpolates into stale memory.**
[`SynthVoice.cpp:161`](../src/engine/SynthVoice.cpp:161) wraps the interpolation
partner index at `kWavBufSize` (2048), not at the active length:

```cpp
int next = (i + 1) & (kWavBufSize - 1);   // wrong: wraps at 2048, not at len
```

so at `i == len-1` it interpolates toward `m_wavBuf[len]`, which holds whatever a
previous, longer waveform left there — a small click on every cycle.
Fix: a guard sample (§3.6).

**D3 — WARP's default is wrong, which means its semantics are wrong.**
The hardware initialises WARP to `00`, not `80` (Appendix A.2). The current code
treats it as bipolar around `0x80`:

```cpp
float warpShift = (ws.warp - 128) / 128.0f;   // = -1.0 at the hardware default
```

so a freshly created WavSynth would start fully warped. A parameter whose
neutral value is `00` is unipolar. The manual — "push the shape to *one* side" —
agrees. Fix: §3.4.

---

## 2. Engine — corrected defaults

`src/engine/Engine.h`, `struct WavSynthState`. Change exactly two lines:

```cpp
    int size  = 0x20;      // was 0x80 -- hardware default, measured 2026-08-17
    int warp  = 0x00;      // was 0x80 -- WARP is unipolar; 00 is neutral (D3)
```

Leave `mult`, `scan`, `filter_type`, `cutoff`, `res`, `amp`, `lim`, `pan`, `dry`,
`cho`, `del`, `rev` alone — all already match the hardware (Appendix A.2).

`tbl_tic` is `0xFF` in our struct and `01` on the device. **Do not change it.**
Every instrument type in this codebase defaults `tbl_tic` to `0xFF`; changing it
for WavSynth alone would make the types inconsistent for no gain. It is recorded
in Appendix A.2 as a known divergence.

---

## 3. Engine — the DSP rebuild

### 3.1 The shape of the fix

One cached table per voice. Everything — SIZE, MULT, WARP, SCAN/mirror, and the
`WAV *` filter modes — is baked into that table when it is generated. Playback is
then a phase accumulator plus one linear interpolation, and costs the same
regardless of the parameters.

The table is regenerated only when a parameter that shapes it changes, or on
note-on. Regeneration is at most 256 samples of straight-line float maths — a few
microseconds — and allocates nothing, so it is safe on the audio thread. It must
stay that way: **no allocation, no `std::string`, no locks** anywhere in this code
(ARCHITECTURE.md hard invariant, enforced by test B8.1).

### 3.2 New members — `src/engine/SynthVoice.h`

Replace the existing `// WavSynth state` block (lines 177–183) with:

```cpp
    // WavSynth state.
    // The table is at most 256 samples because SIZE is a byte and the manual
    // defines it as the sample count of the wave table. The +1 slot is a guard
    // holding a copy of [0], so linear interpolation at the loop point needs no
    // wrap branch and cannot read stale data (Phase 1 defect D2).
    static constexpr int kWavTableMax = 256;
    float    m_wavTable[kWavTableMax + 1] = {};
    int      m_wavTableLen = 0;
    uint32_t m_wavPhase = 0;
    uint32_t m_wavNoiseLfsr = 1u;   // shape 08 (NOISE); reset on note-on
    daisysp::Svf m_wavShaper;       // FILTER 08-0B only -- must NOT be m_filter,
                                    // which is the output-stage filter

    // Cache key: the parameters the current m_wavTable was generated from.
    // -1 means "nothing generated yet". WARP and SCAN are in the key because
    // they are baked into the table (§3.1); cutoff/res only matter when
    // filter_type is 08-0B but are always compared, which is harmless.
    int m_wavKeyShape  = -1;
    int m_wavKeySize   = -1;
    int m_wavKeyMult   = -1;
    int m_wavKeyWarp   = -1;
    int m_wavKeyScan   = -1;
    int m_wavKeyFilter = -1;
    int m_wavKeyCutoff = -1;
    int m_wavKeyRes    = -1;

    bool  wavTableStale(const WavSynthState& ws) const;
    void  regenerateWavTable(const WavSynthState& ws);
    float readWavTable(uint32_t phase) const;
    static float wavBaseShape(int shape, float u);
    static float wavWarpPhase(float u, float warp01);
    static float wavMirrorPhase(float u, float mirror);
    static float wavLfsrNext(uint32_t& state);
```

Delete `kWavBufSize`, `m_wavBuf`, `m_wavBufLen`, `generateWavShape`, and
`readWavBuf`. Before deleting, run
`grep -rn "m_wavBuf\|generateWavShape\|readWavBuf" src tests` — if anything under
`tests/` names them, update that test rather than keeping the old member alive.

In the `SynthVoice::SynthVoice()` constructor, next to the existing
`m_filter.Init(kSampleRate);`, add:

```cpp
    m_wavShaper.Init(kSampleRate);
```

### 3.3 Base shapes — `wavBaseShape()`

One cycle of the raw shape, `u` in `[0,1)`, output in `[-1,+1]`. This is the only
place shape indices are interpreted.

```cpp
// One cycle of a base shape. u in [0,1). Shapes 0-6 are geometric; 7 is the
// pitched LFSR noise table; 8 (NOISE) never reaches here -- it bypasses the
// table entirely (§3.7). Shape indices >= 9 are the 61 built-in wave tables,
// not implemented in this phase: they fall through to SINE, which is what
// Phase 1 did and what status.md documents.
float SynthVoice::wavBaseShape(int shape, float u) {
    switch (shape) {
    case 0: return (u < 0.12f) ? 1.0f : -1.0f;   // PULSE 12%
    case 1: return (u < 0.25f) ? 1.0f : -1.0f;   // PULSE 25%
    case 2: return (u < 0.50f) ? 1.0f : -1.0f;   // PULSE 50%
    case 3: return (u < 0.75f) ? 1.0f : -1.0f;   // PULSE 75%
    case 4: return 2.0f * u - 1.0f;              // SAW
    case 5: return (u < 0.5f) ? (4.0f * u - 1.0f)
                              : (3.0f - 4.0f * u);  // TRIANGLE
    case 7: {                                    // NOISE PITCHED
        // A deterministic hash of the slot position, NOT a running LFSR: the
        // value must depend only on u, so the table is stable and the noise
        // repeats at the note pitch. That periodicity is what makes this shape
        // "in-tune tonal" rather than hiss.
        uint32_t s = static_cast<uint32_t>(u * 4294967296.0f);
        s ^= s >> 16; s *= 2246822519u;
        s ^= s >> 13; s *= 3266489917u;
        s ^= s >> 16;
        return static_cast<float>(s >> 8) * (1.0f / 8388608.0f) - 1.0f;
    }
    case 6:
    default: return std::sin(u * 6.2831853f);    // SINE (and shapes >= 9)
    }
}
```

### 3.4 WARP — `wavWarpPhase()`

WARP is a unipolar skew of the read position: at `00` it is the identity, and as
it rises the first half of the shape is squeezed toward the start of the table
and the second half stretched over the rest — "push the shape to one side".

```cpp
// warp01 in [0,1]. Piecewise-linear, monotonic, continuous, identity at 0.
float SynthVoice::wavWarpPhase(float u, float warp01) {
    if (warp01 <= 0.0f) return u;
    const float pivot = 0.5f * (1.0f - warp01 * 0.98f);   // 0.5 -> ~0.01
    if (u < pivot) return 0.5f * u / pivot;
    return 0.5f + 0.5f * (u - pivot) / (1.0f - pivot);
}
```

The `0.98f` keeps `pivot` away from zero so the division cannot blow up at
`WARP = FF`.

### 3.5 SCAN as mirror — `wavMirrorPhase()`

On the 9 base shapes, SCAN mirrors the waveform at a position, over a 0–200%
range (manual). So the byte maps to `mirror = SCAN / 255 * 2.0` — values above
1.0 put the mirror point past the end of the table, which is how the effect turns
itself off at the top of the range.

Reflected positions are folded back into `[0,1)` rather than clamped; clamping
would flatten the tail of the cycle into DC.

```cpp
// mirror in [0,2]. Positions past the mirror point read back from it.
float SynthVoice::wavMirrorPhase(float u, float mirror) {
    if (mirror <= 0.0f || u <= mirror) return u;
    float v = 2.0f * mirror - u;
    v = std::fabs(v);
    v = std::fmod(v, 2.0f);
    if (v > 1.0f) v = 2.0f - v;
    return v;
}
```

This is what gives PWM on `PULSE 50%`: sweeping the mirror point moves the
boundary between the positive and negative halves, which is exactly what the
manual describes ("for PWM set the shape to PULSE 50% and adjust MIRROR").

### 3.6 Table generation

The transform order matters and is fixed: **MULT builds the repeated shape, WARP
skews it, SCAN mirrors the result.** Written as index transforms, that
composition runs outermost-first, so the code applies mirror, then warp, then the
repeat:

```cpp
bool SynthVoice::wavTableStale(const WavSynthState& ws) const {
    return m_wavKeyShape  != ws.shape  || m_wavKeySize   != ws.size
        || m_wavKeyMult   != ws.mult   || m_wavKeyWarp   != ws.warp
        || m_wavKeyScan   != ws.scan   || m_wavKeyFilter != ws.filter_type
        || m_wavKeyCutoff != ws.cutoff || m_wavKeyRes    != ws.res;
}

void SynthVoice::regenerateWavTable(const WavSynthState& ws) {
    // SIZE is literally the number of samples in the wave table (manual:
    // "Horizontal size of the waveform (number of samples)"). It changes
    // resolution -- the lo-fi stepping that is the WavSynth's character --
    // NOT pitch: phase is normalised over the table whatever its length.
    const int len = std::clamp(ws.size, 4, kWavTableMax - 1);

    // MULT: 1..16 repeats of the shape inside the table. The 1..16 range is
    // this spec's choice, not measured -- see §8 open question O2.
    const int   repeats = 1 + (std::clamp(ws.mult, 0, 255) >> 4);
    const float warp01  = std::clamp(ws.warp, 0, 255) / 255.0f;
    const float mirror  = std::clamp(ws.scan, 0, 255) / 255.0f * 2.0f;
    const int   shape   = std::clamp(ws.shape, 0, 8);   // >= 9 handled in §0

    for (int i = 0; i < len; ++i) {
        float u = static_cast<float>(i) / static_cast<float>(len);
        u = wavMirrorPhase(u, mirror);          // SCAN
        u = wavWarpPhase(u, warp01);            // WARP
        u = u * static_cast<float>(repeats);    // MULT
        u = u - std::floor(u);
        m_wavTable[i] = std::clamp(wavBaseShape(shape, u), -1.0f, 1.0f);
    }

    // FILTER 08-0B ("WAV LP/HP/BP/BS"): the manual says these apply the filter
    // *into the waveform*, so they are a buffer operation here, not an
    // output-stage one. The filter is configured at kSampleRate rather than at
    // the table's playback rate on purpose: the wave table is a buffer, and
    // filtering a buffer is pitch-independent -- which also keeps note pitch
    // out of the cache key, so pitch modulation cannot trigger per-sample
    // regeneration.
    if (ws.filter_type >= 8 && ws.filter_type <= 11) {
        const float cutoffHz = std::clamp(
            20.0f * std::pow(2.0f, (ws.cutoff / 255.0f) * 10.0f), 20.0f, 20000.0f);
        m_wavShaper.SetFreq(cutoffHz);
        m_wavShaper.SetRes(std::clamp(ws.res / 255.0f, 0.0f, 1.0f));

        // Pass 1 warms the filter state over the loop so pass 2 comes out
        // periodic; without it the table starts from silence and the loop point
        // steps. Pass 1's output is deliberately discarded. No scratch buffer
        // is needed: Process() is called before each in-place write.
        for (int i = 0; i < len; ++i) m_wavShaper.Process(m_wavTable[i]);
        for (int i = 0; i < len; ++i) {
            m_wavShaper.Process(m_wavTable[i]);
            switch (ws.filter_type) {
            case 8:  m_wavTable[i] = m_wavShaper.Low();  break;
            case 9:  m_wavTable[i] = m_wavShaper.High(); break;
            case 10: m_wavTable[i] = m_wavShaper.Band(); break;
            case 11: m_wavTable[i] = m_wavTable[i] - m_wavShaper.Band(); break;
            }
        }
    }

    m_wavTable[len] = m_wavTable[0];   // guard sample -- fixes defect D2
    m_wavTableLen = len;

    m_wavKeyShape  = ws.shape;  m_wavKeySize   = ws.size;
    m_wavKeyMult   = ws.mult;   m_wavKeyWarp   = ws.warp;
    m_wavKeyScan   = ws.scan;   m_wavKeyFilter = ws.filter_type;
    m_wavKeyCutoff = ws.cutoff; m_wavKeyRes    = ws.res;
}
```

Table read — no wrap branch, because of the guard sample:

```cpp
float SynthVoice::readWavTable(uint32_t phase) const {
    const float idx = (static_cast<float>(phase) * (1.0f / 4294967296.0f))
                    * static_cast<float>(m_wavTableLen);
    int i = static_cast<int>(idx);
    if (i >= m_wavTableLen) i = m_wavTableLen - 1;   // float-edge guard only
    const float frac = idx - static_cast<float>(i);
    return m_wavTable[i] + (m_wavTable[i + 1] - m_wavTable[i]) * frac;
}
```

LFSR for shape `08`:

```cpp
// 32-bit Galois LFSR. Returns -1..+1. State must never be zero.
float SynthVoice::wavLfsrNext(uint32_t& state) {
    const uint32_t lsb = state & 1u;
    state >>= 1;
    if (lsb) state ^= 0xD0000001u;
    return static_cast<float>(state >> 8) * (1.0f / 8388608.0f) - 1.0f;
}
```

### 3.7 The render path

Replace the whole WavSynth block at
[`SynthVoice.cpp:583`](../src/engine/SynthVoice.cpp:583) with:

```cpp
    bool isWav = false;

    if (m_instrument && m_instrument->type == InstType::INST_WAVSYNTH) {
        isWav = true;
        const WavSynthState& ws = m_instrument->wav;

        if (ws.shape == 8) {
            // NOISE: classic LFSR noise, clocked once per output sample and so
            // independent of note pitch. Shape 07 (NOISE PITCHED) is the tonal
            // one and goes through the table like every other shape.
            sample = wavLfsrNext(m_wavNoiseLfsr);
        } else {
            if (wavTableStale(ws)) regenerateWavTable(ws);

            const float noteFreq = m_frequency
                * std::pow(2.0f, (mt.pitch + m_tableTranspose) / 12.0f);
            m_wavPhase += static_cast<uint32_t>(
                (noteFreq / kSampleRate) * 4294967296.0f);
            sample = readWavTable(m_wavPhase);
        }
    }
```

The output-stage tail at [`SynthVoice.cpp:648`](../src/engine/SynthVoice.cpp:648)
stays exactly as it is, including the `stdFilter` line and its comment — filter
modes `08`–`0B` are still baked into the table, so they still map to "no
output-stage filter". Do not touch it.

### 3.8 Note-on determinism

In `SynthVoice::noteOn()`, replace the existing WavSynth block
([`SynthVoice.cpp:208`](../src/engine/SynthVoice.cpp:208)) with:

```cpp
    if (m_instrument && m_instrument->type == InstType::INST_WAVSYNTH) {
        m_wavPhase = 0;
        m_wavNoiseLfsr = 1u;                       // determinism, see below
        regenerateWavTable(m_instrument->wav);
    }
```

**Both resets are load-bearing.** `tests/ui/manifest.txt` has a `diff:` policy
that renders the same song through the live app and through `m8_render` and
requires the two WAVs to be **byte-identical**. Any voice state that survives a
note-on, or that depends on how many samples were rendered before it, breaks that
identity. Seed the LFSR to a fixed non-zero value on every note-on and never let
it run across notes.

---

## 4. `SongIO.cpp` — new-song defaults

In `buildSongFromEngine()` at [`SongIO.cpp:1266`](../src/io/SongIO.cpp:1266), the
`INST_WAVSYNTH` branch writes literal defaults. Bring them in line with §2:

```cpp
    wvs.size = 0x20;      // was 0x80
    wvs.warp = 0x00;      // was 0x80
```

Do not touch the load or save conversions — they copy whatever the struct holds
and are already correct.

---

## 5. UI — the WavSynth instrument screen

> **This section was rewritten on 2026-08-17** after four commits landed from
> another machine adding the HyperSynth and FMSynth instrument screens. The
> earlier version described a two-way `isMac` screen that no longer exists, and
> told you to build machinery that is now unnecessary. What follows matches the
> tree as it actually is.

### 5.0 What the instrument screen looks like now

`InstrumentScreen.cpp` is 619 lines and already dispatches **four** ways —
Sampler, Macrosyn, FMSynth, HyperSynth — through nested ternaries of the form:

```cpp
    bool isMac = (inst.type == engine::InstType::INST_MACROSYN);
    bool isHyp = (inst.type == engine::InstType::INST_HYPERSYN);
    bool isFm  = (inst.type == engine::InstType::INST_FMSYNTH);
    ... isHyp ? inst.hyper.X : (isMac ? inst.macrosyn.X : (isFm ? inst.fm.X : inst.sampler.X))
```

There are 87 of these tests across five functions. **Follow that pattern — add
`isWav` to the chain.** Do not refactor it into a dispatch table or an enum:
that is a change to four working screens this phase does not own, and it is not
why you are here. WavSynth is the fifth and last type that needs a screen.

Three things are already done for you:

- `ResolveInstrumentValue` already returns `"WAVSYNTH"` for `C::TYPE`
  ([`InstrumentScreen.cpp:135`](../src/ui/screens/instrument/InstrumentScreen.cpp:135)).
- File-scope enum name tables already exist: `kFilterModes[8]`, `kLimModes[9]`
  (all nine, including `POST:W1`–`W3`), `kPlayModes[15]`, `kMacroShapes[44]`.
- The shared `C::LIM` clamp is already `0, 8`, which is what WavSynth needs.

### 5.1 The layout, measured off the device

Hardware capture (Appendix A.1) gives absolute cell coordinates; this codebase's
layouts sit at **−3 rows and −1 column** from those. The table below is already
converted.

| Field | Label | Value | Accent / slider |
|---|---|---|---|
| TYPE     | `TYPE` 0,2      | `WAVSYNTH` 8,2 | — |
| CMD_LOAD | —               | `LOAD` 22,2 | — |
| CMD_SAVE | —               | `SAVE` 27,2 | — |
| NAME     | `NAME` 0,3      | 8,3  | — |
| TRANSP   | `TRANSP.` 0,4   | 8,4  | — |
| TBL_TIC  | `TBL.TIC` 13,4  | 21,4 | — |
| EQ       | `EQ` 26,4       | 29,4 | — |
| SHAPE    | `SHAPE` 0,6     | 8,6  | accent 10,6 |
| WAV_SIZE | `SIZE` 0,8      | 8,8  | slider 10,8 |
| WAV_MULT | `MULT` 0,9      | 8,9  | slider 10,9 |
| WAV_WARP | `WARP` 0,10     | 8,10 | slider 10,10 |
| WAV_SCAN | `SCAN` 0,11     | 8,11 | slider 10,11 |
| FILTER   | `FILTER` 0,12   | 8,12 | accent 10,12 |
| CUTOFF   | `CUTOFF` 0,13   | 8,13 | slider 10,13 |
| RES      | `RES` 0,14      | 8,14 | slider 10,14 |
| AMP      | `AMP` 17,8      | 21,8  | slider 23,8 |
| LIM      | `LIM` 17,9      | 21,9  | accent 23,9 |
| PAN      | `PAN` 17,10     | 21,10 | center_tick at 26,10 |
| DRY      | `DRY` 17,11     | 21,11 | slider 23,11 |
| MFX      | `MFX` 17,12     | 21,12 | slider 23,12 |
| DEL      | `DEL` 17,13     | 21,13 | slider 23,13 |
| REV      | `REV` 17,14     | 21,14 | slider 23,14 |

The right-hand column is at **17/21/23**, matching Sampler, Macrosyn, FMSynth and
HyperSynth. The hardware capture puts it one column further right, but making
WavSynth the only screen that disagrees with the other four would be worse than
the offset itself. Recorded as open question O4 — which now applies to all five
screens equally rather than to this one.

PAN gets a `center_tick` rather than a slider, following HyperSynth: it is a
bipolar parameter and the newer layouts mark those with a `|` instead of a fill.

### 5.2 `InstrumentCursorId.h`

Add four ids at the end, after the HyperSynth block. Do not renumber or reorder
anything that exists.

```cpp
    // Wavsynth-only (SHAPE, FILTER, CUTOFF, RES and the whole right-hand
    // column are shared with the other layouts)
    WAV_SIZE, WAV_MULT, WAV_WARP, WAV_SCAN,
```

Note the `WAV_` prefix: bare `SIZE`/`SCAN` would read ambiguously beside the
Sampler's `START`/`LENGTH` and the FM operator ids. `SHAPE` is deliberately
**reused** from the Macrosyn block — same screen position, same role, and the
resolvers already branch on instrument type.

Update the comment above the enum; it still says "Shared between the Sampler and
Macrosyn layouts" and is now three types out of date.

### 5.3 `InstrumentWavsynthLayout.h` (new file)

**Copy `InstrumentHypersynLayout.h`, not the Macrosyn one.** It is the most
recent sibling, its shape is closest (a left parameter column and a right mixer
column), and it carries the current conventions that Macrosyn predates:

- Every **label** lives in `GetWavsynthStaticText()`.
  `GetWavsynthInteractiveFields()` holds only values, accents and sliders.
  Macrosyn mixes labels into the interactive entries; do not copy that.
- Colour roles: labels `"LABEL_DIM"/"LABEL_LITE"`, values `"VALUE"/"LABEL_LITE"`,
  accents `"ACCENT"/"LABEL_LITE"`, sliders `"SLIDER"/"SLIDER"` with `width = 6`.
- The mixer send is labelled **`MFX`**, which HyperSynth already does. The engine
  field stays `wav.cho` and the cursor id stays `C::CHO`; only the label differs.
- The header block (TYPE / CMD_LOAD / CMD_SAVE / NAME / TRANSP / TBL_TIC / EQ)
  can be copied verbatim — those coordinates are identical across all layouts.

Four functions, same names with `Wavsynth` substituted: `GetWavsynthStaticText`,
`GetWavsynthDynamicTextDefaults`, `GetWavsynthInteractiveFields`,
`GetWavsynthNavMap`.

Navigation — the vertical chain was read off the device with `m8drv probe DOWN`,
so follow it exactly:

- Left column: `TYPE → NAME → TRANSP → SHAPE → WAV_SIZE → WAV_MULT → WAV_WARP →
  WAV_SCAN → FILTER → CUTOFF → RES`
- Right column: `AMP → LIM → PAN → DRY → CHO → DEL → REV`
- Header rows: `TYPE ↔ CMD_LOAD ↔ CMD_SAVE`, and `TRANSP ↔ TBL_TIC ↔ EQ`
- Left/right pairing by screen row: `WAV_SIZE↔AMP`, `WAV_MULT↔LIM`,
  `WAV_WARP↔PAN`, `WAV_SCAN↔DRY`, `FILTER↔CHO`, `CUTOFF↔DEL`, `RES↔REV`.
  `SHAPE`'s right neighbour is `AMP`.

Also add the shape-name table to this header:

```cpp
// All 70 SHAPE names as fw 6.5.2 draws them (Appendix B). Shapes >= 0x09 do not
// sound until Phase 3 -- they alias to sine in the engine -- but a loaded .m8s
// can carry one, and showing "SINE" for WT-EFX:CYBERNET would be wrong on
// screen. Padded to a constant width so a shorter name cannot leave characters
// of a longer one behind.
inline const char* WavShapeName(int shape);
```

### 5.4 `InstrumentScreen.cpp`

Add `#include "InstrumentWavsynthLayout.h"` and one more file-scope table beside
`kFilterModes`:

```cpp
// WavSynth's FILTER has 12 entries, not 8: it adds the four "WAV" modes that
// filter the wave table itself. These are the strings fw 6.5.2 draws on the
// WavSynth screen, including the truncations (HIGHPAS, not HIGHPASS). They are
// deliberately a separate table rather than an extension of kFilterModes: the
// first eight entries there are this codebase's own abbreviations ("LP ", "HP ")
// and have not been verified against what the device draws on the other four
// screens, so widening that table would change screens this phase does not own.
static const char* const kWavFilterModes[12] = {
    "OFF    ", "LOWPASS", "HIGHPAS", "BANDPAS", "BANDSTP", "LP>HP  ",
    "ZDF LP ", "ZDF HP ", "WAV LP ", "WAV HP ", "WAV BP ", "WAV BS "
};
```

Then add `bool isWav = (inst.type == engine::InstType::INST_WAVSYNTH);` to each of
these five functions and extend their chains.

**`ResolveInstrumentValue`** — add `isWav` to every shared-field ternary
(`TRANSP`, `TBL_TIC`, `EQ`, `FILTER`, `LIM`, `CUTOFF`, `RES`, `AMP`, `PAN`,
`DRY`, `CHO`, `DEL`, `REV`). `C::TYPE` already handles it. Then:

```cpp
    if (isWav && fieldId == C::SHAPE) return ToHex(inst.wav.shape);
    if (fieldId == C::WAV_SIZE) return ToHex(inst.wav.size);
    if (fieldId == C::WAV_MULT) return ToHex(inst.wav.mult);
    if (fieldId == C::WAV_WARP) return ToHex(inst.wav.warp);
    if (fieldId == C::WAV_SCAN) return ToHex(inst.wav.scan);
```

The existing `if (fieldId == C::SHAPE) return ToHex(inst.macrosyn.shape);` must
come **after** the `isWav` line, or WavSynth will display the Macrosyn shape byte.

**`ResolveInstrumentAccent`** — the `C::FILTER`, `C::LIM` and `C::SHAPE` branches
each need a WavSynth case ahead of the existing one:

```cpp
    if (isWav && fieldId == C::FILTER) {
        int f = inst.wav.filter_type;
        if (f >= 0 && f < 12) return kWavFilterModes[f];
    }
    if (isWav && fieldId == C::LIM) {
        int l = inst.wav.lim;
        if (l >= 0 && l < 9) return kLimModes[l];      // already complete
    }
    if (isWav && fieldId == C::SHAPE) return WavShapeName(inst.wav.shape);
```

**`GetSliderValue`** — add `isWav` to the shared ternaries, plus:

```cpp
    if (fieldId == C::WAV_SIZE) return inst.wav.size;
    if (fieldId == C::WAV_MULT) return inst.wav.mult;
    if (fieldId == C::WAV_WARP) return inst.wav.warp;
    if (fieldId == C::WAV_SCAN) return inst.wav.scan;
```

`SHAPE`, `FILTER` and `LIM` have accents, not sliders — leave them returning 0.

**`RenderInstrumentScreen`** — extend the three layout-getter ternaries with the
`Wavsynth` variants, keeping the existing nesting order.

**`HandleInstrumentInput`** — extend the `navMap` ternary the same way.

### 5.5 Input handling

The TYPE cycle is at
[`InstrumentScreen.cpp:471`](../src/ui/screens/instrument/InstrumentScreen.cpp:471):

```cpp
    int newType = std::clamp<int>(static_cast<int>(inst.type) + step, 0, 3);
```

`InstType` is ordered `{SAMPLER, MACROSYN, HYPERSYN, FMSYNTH, WAVSYNTH, MIDI,
NONE}`, so `0, 3` reaches the four types that have screens today. **Widen it to
`0, 4`** — that is the whole change, because WAVSYNTH is index 4 and every type
below it now has a layout.

> An earlier draft of this spec had you build a list of "editable types" to skip
> HyperSynth and FMSynth, which had no screens when it was written. That
> machinery is obsolete. Do not build it.

Then add the WavSynth edit branches. Shared branches need per-type clamps only
where WavSynth's range is wider:

| Field | ParamID | Range | Note |
|---|---|---|---|
| SHAPE | `WAV_SHAPE` | 0–8 | **Phase 2 clamp.** Phase 3 widens it to `0x45`. Leave a `// TODO(phase3)` on this line. |
| WAV_SIZE | `WAV_SIZE` | 0–255 | |
| WAV_MULT | `WAV_MULT` | 0–255 | |
| WAV_WARP | `WAV_WARP` | 0–255 | |
| WAV_SCAN | `WAV_SCAN` | 0–255 | |
| FILTER | `INST_FILTER` | **0–11** for WavSynth | The shared branch clamps `0, 7`; the four `WAV` modes sit above that. |
| LIM | `INST_LIM` | 0–8 | Already correct — the shared clamp is `0, 8`. No change. |

Write the FILTER clamp as `isWav ? 11 : 7` rather than widening it for everybody:
the other four screens have only eight filter modes and would gain four
unreachable ones.

Nothing else in the input handler needs touching. The FM operator clipboard, the
HyperSynth chord clipboard and the NAME character cursor all key off cursor ids
WavSynth does not use, and fall through untouched.

## 6. Tests

### 6.1 Keep the existing five

`tests/test_wavsynth.cpp`'s five cases must still pass unmodified. If one fails,
the DSP change is wrong — do not weaken the assertion (AGENTS.md §4).

### 6.2 New engine cases, same file, `[wavsynth]` tag

Accumulate a flag and assert once per case; never assert inside a per-sample loop.

1. **`WavSynth table is regenerated only when a shaping parameter changes`** —
   render 500 samples, flip `ws.warp`, render 500 more, require the two halves
   differ. Then render 1000 samples with no parameter change and require the
   second half to match the first period-for-period (the table did not drift).
2. **`WavSynth loop point is continuous`** — shape 6 (SINE), SIZE `0x20`, a low
   note so many output samples fall per cycle. Track the largest absolute
   sample-to-sample step across 20000 samples and require it below the largest
   step a clean sine of that period could produce, times a small margin. This is
   the regression test for defect D2; it fails against the Phase 1 code.
3. **`WavSynth WARP 00 is the identity`** — assert `wavWarpPhase(u, 0.0f) == u`
   across a spread of `u`, plus a render-level check that `warp = 0x00` and the
   old default `0x80` now differ (they must, since `0x80` is no longer neutral).
4. **`WavSynth SCAN mirrors PULSE 50% into PWM`** — shape 2, SCAN at `0x20`,
   `0x40`, `0x60`; require the mean (DC offset) of the rendered block to differ
   between them. A duty-cycle change shows up as DC.
5. **`WavSynth NOISE is not periodic and NOISE PITCHED is`** — shape 8 vs shape 7
   at the same note. Correlate each block against itself shifted by one period:
   shape 7 must correlate strongly, shape 8 must not.
6. **`WavSynth is deterministic across renders`** — render the same note twice
   from two fresh `OfflineHost`s with shape 8 and require the buffers to be
   byte-identical. This is what protects the `diff:` manifest policy (§3.8).
7. **`WavSynth RT safety`** — extend the existing zero-allocation case to change a
   parameter mid-render, so `regenerateWavTable` runs inside `render()`, and still
   require `g_allocCount == 0`.

### 6.3 New UI script — `tests/ui/wavsynth_screen.m8script`

The runner discovers every `tests/ui/*.m8script` automatically; a script not
listed in `manifest.txt` defaults to "must exit 0", which is what we want, so
**do not add a manifest line**.

`tests/ui/fixtures/probe_wavsynth.m8s` already exists in the tree and is
currently unreferenced — check whether its instrument 00 is a WavSynth (load it,
or inspect with `m8_makeprobe`). If it is, `load` it; if not, reach WavSynth by
cycling TYPE from the default song instead.

```
goto INSTRUMENT
assert_screen contains "INST."

# TYPE cycles SAMPLER -> MACROSYN -> HYPERSYN -> FMSYNTH -> WAVSYNTH
key X
key RIGHT
key RIGHT
key RIGHT
key RIGHT
assert_screen row 2 contains "WAVSYNTH"

# The five WavSynth-specific rows, at the coordinates measured on fw 6.5.2
assert_screen row 6 contains "SHAPE"
assert_screen row 6 contains "PULSE 12%"
assert_screen row 8 contains "SIZE"
assert_screen row 9 contains "MULT"
assert_screen row 10 contains "WARP"
assert_screen row 11 contains "SCAN"
assert_screen row 12 contains "FILTER"

# The mixer column uses the hardware label MFX, not CHO
assert_screen row 12 contains "MFX"

# Navigate the left column down to SIZE
key DOWN
key DOWN
key DOWN
key DOWN
assert_screen row 8 contains "SIZE"
```

`key X` is the harness's EDIT hold — confirm the idiom against an existing script
such as `tests/ui/edit.m8script` and copy it rather than guessing. Available
verbs are parsed in `src/ui/ScriptRunner.cpp`; `assert_slider` and
`assert_cell_color` exist too if a stronger assertion is wanted.

---

## 7. Build and verify

Two build directories exist, `build/` and `build_asan/`. Never create a third.
Always pass `--target`.

```bash
cmake --build build --config Release --target m8_tests
```

```bash
cmake --build build --config Release --target m8_clone
```

While iterating, run only the relevant tag, compact:

```bash
./build/Release/m8_tests.exe "[wavsynth]" --reporter compact
```

Once, at the end, when the work is complete:

```bash
./build/Release/m8_tests.exe --reporter compact
```

`--reporter compact` prints one line per failure and nothing on success. Report
pass/fail counts and the text of real failures — do not paste passing output.

ASan is not needed for this change: it touches no memory ownership, no sample
pool, no rings. Say so plainly rather than running it as a ritual.

---

## 8. Open questions — record, do not guess

These are unmeasured. Implement what this spec says, leave the marker comment,
and restate them in the completion report so they can be A/B'd against hardware
later.

- **O1 — SIZE.** Read literally from the manual as the table's sample count
  (§3.6). Plausible, and it produces the right lo-fi character, but the mapping
  from the byte has not been compared against the device.
- **O2 — MULT.** `1 + (MULT >> 4)`, giving 1–16 repeats. Both the range and the
  quantisation are this spec's choice. The M8's "hard sync" character suggests it
  may go considerably higher.
- **O3 — WARP curve.** The piecewise-linear skew is a guess at the shape of the
  curve. Its *neutral point* (`00`) is measured and is not in question.
- **O4 — right-column x-position.** All five instrument layouts draw their right
  column one cell left of where the hardware capture puts it (§5.1). This spec
  keeps WavSynth consistent with its four siblings rather than correcting one
  screen in isolation; unifying them against hardware is a separate, five-screen
  change nobody has asked for yet.
- **O5 — enum name visibility.** On a settled screen the device draws `FILTER 00`
  and `LIM 00` with **no** enum name, but reports `00OFF` / `00CLIP` when the
  cursor is on the field. `SHAPE` always shows its name. Our screens always draw
  the name. Not worth chasing; recorded because a future pixel-fidelity
  comparison will trip over it.

---

## Appendix A — hardware evidence

All measured 2026-08-17 against a real M8, firmware **6.5.2**, on COM3, via
`python tools/m8drv/m8drv.py`. Instrument 00 of whatever project the device had
loaded was switched to WAVSYNTH for the readings and **restored to HYPERSYN**
afterwards; nothing was written to the SD card.

### A.1 The WAVSYNTH instrument screen, absolute cell coordinates

`m8drv capture` output, rows 3–17, with a column ruler. Subtract 3 from the row
and 1 from the column for this codebase's layout coordinates.

```
    0000000000111111111122222222223333333333
    0123456789012345678901234567890123456789
  3 | INST. 00                               |
  5 | TYPE    WAVSYNTH      LOAD SAVE  T>120 |
  6 | NAME    ------------                   |
  7 | TRANSP. ON   TBL.TIC 01   EQ --  1 --- |
  9 | SHAPE   00PULSE 12%              3 --- |
 11 | SIZE    20        AMP 00         5 --- |
 12 | MULT    00        LIM 00         6 --- |
 13 | WARP    00        PAN 80         7 --- |
 14 | SCAN    00        DRY C0         8 --- |
 15 | FILTER  00        MFX 00               |
 16 | CUTOFF  FF        DEL 00               |
 17 | RES     00        REV 00               |
```

Text runs, with palette indices: left labels at col 1 fg4, values at col 9 fg7,
the SHAPE accent at col 11 fg6; right-hand labels at col 19 fg4 and values at
col 23 fg7.

Sliders are filled rects, `h_px = 5`, `off_y = 4`, at col 11 (left column) and
col 25 (right column). Measured widths: `SIZE 0x20 → 6px`, `DRY 0xC0 → 39px`,
`CUTOFF 0xFF → 52px`. So the device's full-scale slider is 52 px ≈ 6.5 cells;
this codebase uses 6 cells (48 px) everywhere, and this spec keeps that.

**There is no waveform preview on the WavSynth screen.** The only non-text
elements are those three slider rects. Confirmed on a fresh connection (full
repaint) with a wave-table shape selected, so it is not a partial-redraw
artifact.

### A.2 Defaults on a freshly created WAVSYNTH

`TRANSP. ON`, `TBL.TIC 01`, `EQ --`, `SHAPE 00`, `SIZE 20`, `MULT 00`,
`WARP 00`, `SCAN 00`, `FILTER 00`, `CUTOFF FF`, `RES 00`, `AMP 00`, `LIM 00`,
`PAN 80`, `DRY C0`, `MFX 00`, `DEL 00`, `REV 00`.

Divergences from `WavSynthState`'s defaults: `size` (ours `0x80`) and `warp`
(ours `0x80`) — both corrected in §2. `tbl_tic` (ours `0xFF`) — deliberately left
alone, see §2.

### A.3 Enum tables, read by stepping the value with EDIT+RIGHT

**Instrument TYPE**, in device order — note it is *not* our `InstType` order:

```
NONE, WAVSYNTH, MACROSYN, SAMPLER, FMSYNTH, HYPERSYN, MIDI OUT, EXTERNAL
```

**FILTER** (12 values): `00 OFF`, `01 LOWPASS`, `02 HIGHPAS`, `03 BANDPAS`,
`04 BANDSTP`, `05 LP>HP`, `06 ZDF LP`, `07 ZDF HP`, `08 WAV LP`, `09 WAV HP`,
`0A WAV BP`, `0B WAV BS`.

**LIM** (9 values): `00 CLIP`, `01 SIN`, `02 FOLD`, `03 WRAP`, `04 POST`,
`05 POST:AD`, `06 POST:W1`, `07 POST:W2`, `08 POST:W3`.

Both lists match our `filter_type` and `lim` numbering exactly.

### A.4 Cursor chain, from `m8drv probe DOWN`

`TYPE → NAME → TRANSP → SHAPE → SIZE → MULT → WARP → SCAN → FILTER → CUTOFF`
(and on to `RES`).

### A.5 Two driver findings made while doing this

`m8drv set <FIELD> <ENUM>` walks the enum **forward only** and does not wrap, so
any target sitting *before* the current value is unreachable: `set TYPE WAVSYNTH`
from HYPERSYN walked up to EXTERNAL, hit the end stop, and failed with
`could not find enum 'WAVSYNTH'` — having moved the field to EXTERNAL on the way.
`editValue`'s enum loop is at
[`Primitives.cpp:1200`](../src/tools/m8/Primitives.cpp:1200).

Named-field navigation (`cursor <FIELD>`) does not work on the WavSynth
instrument screen at all — `ScreenModel` knows only the SAMPLER and MACROSYN
variants ([`ScreenModel.h:663`](../src/tools/m8/ScreenModel.h:663)) — so this
session drove it with raw `PRESS` keys.

Neither is in scope here; both belong in `M8_DRIVER_BUGS.md`.

---

## Appendix B — all 70 SHAPE names

Read off fw 6.5.2 by stepping SHAPE from `00` to the end stop. Shapes `00`–`08`
are implemented in this phase; `09`–`45` are Phase 3 and currently alias to sine,
but their names are wired into the UI now.

```
00 PULSE 12%        17 WT-OSC:UMBRELLA   2E WT-HRM:TWINE
01 PULSE 25%        18 WT-OSC:UNWIND     2F WT-EFX:ALIEN
02 PULSE 50%        19 WT-OSC:VIRAL      30 WT-EFX:CYBERNET
03 PULSE 75%        1A WT-OSC:WAVES      31 WT-EFX:DISORDR
04 SAW              1B WT-BNK:DRIP       32 WT-EFX:FORMANT
05 TRIANGLE         1C WT-BNK:FROGGY     33 WT-EFX:HYPER
06 SINE             1D WT-BNK:INSONIC    34 WT-EFX:JAGGED
07 NOISE PITCHED    1E WT-BNK:RADIUS     35 WT-EFX:MIXED
08 NOISE            1F WT-BNK:SCRATCH    36 WT-EFX:MULTIPLY
09 WT-OSC:CRUSH     20 WT-BNK:SMOOTH     37 WT-EFX:NOWHERE
0A WT-OSC:FOLDING   21 WT-BNK:WOBBLE     38 WT-EFX:PINBALL
0B WT-OSC:FREQ      22 WT-HRM:ASYMMTRY   39 WT-EFX:RINGS
0C WT-OSC:FUZZY     23 WT-HRM:BLEEN      3A WT-EFX:SHIMMER
0D WT-OSC:GHOST     24 WT-HRM:FRACTAL    3B WT-EFX:SPECTRAL
0E WT-OSC:GRAPHIC   25 WT-HRM:GENTLE     3C WT-EFX:SPOOKY
0F WT-OSC:LFOPLAY   26 WT-HRM:HARMONIC   3D WT-EFX:TRANSFRM
10 WT-OSC:LIQUID    27 WT-HRM:HYPNOTIC   3E WT-EFX:TWISTED
11 WT-OSC:MORPHING  28 WT-HRM:ITERATIV   3F WT-EFX:VOCAL
12 WT-OSC:MYSTIC    29 WT-HRM:MICROWAV   40 WT-EFX:WASHED
13 WT-OSC:STICKY    2A WT-HRM:PLAITS01   41 WT-EFX:WONDER
14 WT-OSC:TIDAL     2B WT-HRM:PLAITS02   42 WT-EFX:WOWEE
15 WT-OSC:TIDY      2C WT-HRM:RISEFALL   43 WT-EFX:ZAP
16 WT-OSC:TUBE      2D WT-HRM:TONAL      44 WT-VOX:BRAIDS
                                         45 WT-VOX:VOXSYNTH
```

The order matches `m8::WavShape` in
[`third_party/m8-files-cxx/src/synths.hpp:167`](../third_party/m8-files-cxx/src/synths.hpp:167)
exactly, so `static_cast<WavShape>(index)` stays valid. One cosmetic mismatch:
the library spells `0x22` `WtAsimmitry`; the device draws `ASYMMTRY`. Use the
device spelling in the UI table and leave the library enum alone.

---

## Appendix C — completion checklist

- [x] §2 defaults corrected in `Engine.h`
- [x] §3 DSP rebuilt; `m_wavBuf` / `generateWavShape` / `readWavBuf` gone
- [x] §3.8 note-on resets both `m_wavPhase` and `m_wavNoiseLfsr`
- [x] §4 `SongIO.cpp` new-song defaults corrected
- [x] §5.2 four `WAV_*` cursor ids added
- [x] §5.3 `InstrumentWavsynthLayout.h` created from the HyperSynth template, including `WavShapeName`
- [x] §5.4 `isWav` added to the existing four-way ternary chains, `kWavFilterModes` added
- [x] §5.5 TYPE clamp widened to `0, 4`; WavSynth fields editable; FILTER clamp `isWav ? 11 : 7`
- [x] §6.2 seven new engine cases, existing five untouched and passing
- [x] §6.3 UI script added (no manifest line)
- [x] Full suite run once, counts reported
- [x] O1–O5 restated in the completion report as still unmeasured
- [x] `status.md` WavSynth entry updated; `WAVSYNTH_IMPLEMENTATION.md` marked
      superseded for its §3
