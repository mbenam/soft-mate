# Bug investigation: TEST-FILE.m8s fails m8_analyze (DC / crest / clipping)

**Status:** Fixed. Fixes 1–3 from §6 applied and verified (§8); fix 4 (MacroSynth
fidelity) is a separate, larger effort and remains open.
**Date:** 2026-07-16
**Scope:** `src/engine/SynthVoice.cpp`, `src/engine/Engine.cpp`, `src/tools/main_render.cpp`
**Companion doc:** see `ARCHITECTURE.md` for the overall engine architecture and invariants.

---

## 1. Symptom

Rendering the unmodified example song
`third_party/m8-files-cxx/examples/songs/TEST-FILE.m8s` via
`m8_render --load ... --song` produces audio that fails **all four** of
`m8_analyze`'s hard checks:

```
peak 1.000  rms 0.7734  clipped 637  non-finite 0
FAIL clipped 532 != 0
FAIL |DC| 0.063667 >= 0.005 L
FAIL |DC| 0.053749 >= 0.005 R
FAIL DC worst-window 0.144147 >= 0.01
FAIL crest 2.20 dB <= 6 dB
```

The demo song (no `--load`) passes. An earlier investigation concluded this was
a "global, non-content-dependent signal present from sample 0 across all
tracks, appearing specifically via LOAD_SONG." The symptoms are real, but that
conclusion is wrong — it was produced by broken isolation tooling (see §4).

---

## 2. Root cause

**Unimplemented instrument types render as a full-scale, un-enveloped,
never-ending sawtooth.**

The chain of facts:

1. `SongIO.cpp` maps every instrument type the engine doesn't implement
   (FMSynth, HyperSynth, WavSynth, MIDIOut, External) to
   `InstType::INST_NONE`.

2. `Engine::tickTrack` (`src/engine/Engine.cpp:345`) never checks the
   instrument type before triggering. Any phrase step with a note calls
   `m_voices[t].noteOn(...)` regardless of what the instrument is.

3. `SynthVoice::renderSample` (`src/engine/SynthVoice.cpp:214`) has **no type
   gate on the oscillator fall-through**. The sampler branch returns early,
   but everything else falls through to:

   ```cpp
   m_osc.SetFreq(m_frequency * std::pow(2.0f, mt.pitch / 12.0f));
   float sample = m_osc.Process();          // polyBLEP saw — unconditional
   if (... type == INST_MACROSYN) { ... }   // extra processing only; not a gate
   ```

   `INST_NONE`, `INST_MIDI`, and null-instrument voices all render the raw
   saw. Since `INST_NONE` instruments get **no modulators** from the file
   conversion (mods stay default: `dest = OFF`), there is no volume envelope:
   `volMod = gate * (1 + 0) = 1`. The note sustains at full amplitude forever
   (nothing sends a note-off at phrase end; only KIL/new-note/empty-chain do).

4. TEST-FILE is a *type-coverage* song. Its converted instrument list
   (dumped during this investigation):

   ```
   inst 00 type=NONE      '------------'
   inst 01 type=NONE      'WAV'
   inst 02 type=MACROSYN  'MAC'        (mods0 = TrigEnv -> MOD_AMT, no volume env)
   inst 03 type=SAMPLER   'SAMP'       (sample missing: TR505 bass drum -> silent)
   inst 04 type=NONE      'FM'
   inst 05 type=NONE      'HYPER'
   inst 06 type=NONE      'MIDI'
   inst 07 type=NONE      'EXT_INST'
   ```

   Its phrases trigger C-2 (65.4 Hz) on 7 tracks at sample 0 (confirmed in the
   event CSV). Most of those notes land on `INST_NONE` instruments.

**Resulting signal:** five-plus identical, phase-locked, constant 65.4 Hz saws
sum on the master bus → `tanh` saturates → near-square wave. That single
mechanism produces every reported number:

- **peak ≈ 1.0, clipped 637** — bus saturation (`kBusAtten` is 1.0, a no-op).
- **crest 2.2 dB** — a saturated square has ~0 dB crest.
- **"DC" L/R and worst-window 0.144** — the window *mean* of a loud sustained
  65 Hz square (non-integer number of periods per window) plus saturation
  asymmetry. The master DC-blocker (`c = 0.9998`, τ ≈ 100 ms) only partially
  removes it. It is a symptom of the drone, not an independent DC source.
- **"present from sample 0"** — the first tick fires at sample 0 and triggers
  the notes.
- **demo song unaffected** — all demo instruments are real SAMPLER/MACROSYN
  with AHD volume envelopes, so nothing drones.
- **"appears specifically via LOAD_SONG"** — only loaded songs can contain
  unimplemented instrument types.

---

## 3. Experimental proof

A temporary one-line gate in `SynthVoice::renderSample` returning `0.0f` for
any non-`INST_MACROSYN` instrument on the oscillator path, same render:

| metric        | before  | after gate |
|---------------|---------|------------|
| clipped       | 637     | **0**      |
| DC L          | 0.0637  | **0.0087** |
| DC R          | 0.0537  | **0.0029** |
| DC worst-1s   | 0.1441  | 0.0145     |
| crest         | 2.20 dB | 4.03 dB    |

The residual (rms 0.62, crest 4 dB) is the *legitimate* `MAC` instrument
(inst 02): its file mods contain no volume envelope (TrigEnv → MOD_AMT), so it
also sustains a saw. That part is the known "MacroSynth is a stub" limitation
(see ARCHITECTURE.md §4), aggravated by the `sp.volume → amp` mapping in
`SongIO.cpp` which turns a file volume of 0x80 into ~4.5× drive
(`ampVal = 1 + (amp/255) * 7`). Not memory corruption.

All temporary experiment patches were reverted; the working tree is unchanged.

---

## 4. Why the earlier isolation went wrong (tooling bugs)

These three bugs manufactured the "global, non-content-dependent,
not-from-any-track" narrative:

### 4.1 `m8_render --solo` is a no-op when combined with `--load`

`renderOnce` (`src/tools/main_render.cpp`) mutes other tracks by writing
`mixer.track_vol[t] = 0` **directly into engine state** via
`getStateForInit()` *before* rendering. But `setupEngine` has already queued a
`LOAD_SONG` command, which is processed on the **first rendered sample** and
overwrites the entire mixer with the song's mixer — wiping the mute.

Verified: solos of tracks 0, 2, and 7 produce **byte-identical** output
(peak 0.998, rms 0.7734 each). So:

- "all 7 active tracks produce identical peak/DC when soloed" — because solo
  did nothing; every render was the full mix.
- "track 7 (zero NOTE_ONs) produces peak 0.998" — same reason. Track 7
  genuinely produces silence.

Solo works for the demo song only because `loadDemoSong()` runs immediately
and no LOAD_SONG command later overwrites the mixer.

**Fix:** apply solo *after* LOAD_SONG — e.g. push `UPDATE_PARAM /
MIX_TRK_VOL` commands into the ring after the LOAD_SONG command, or apply the
mute after a warm-up render call.

### 4.2 `printTrackInfo` prints pre-LOAD engine state

It reads `engine.getStateForInit()` before the LOAD_SONG command has been
processed, so for loaded songs the "instrument table" shows 128
default-constructed SAMPLERs (`------------  SAMPLER  sample: (none)`) and
hides the actual instruments. It also `continue`s past `INST_NONE`, hiding
exactly the instruments that cause this bug. Anyone debugging from this dump
concludes no instrument could be making sound.

**Fix:** print after commands are processed (post-warm-up), and print
`INST_NONE` entries explicitly instead of skipping them.

### 4.3 `EngineEvent.instrument` holds the sample handle, not the instrument index

`Engine.cpp:403` sets `e_on.instrument` to `sampler.sample` (a
`SampleHandle`; −1 → 255 in the uint8 field) for samplers and `0` for
everything else. The event CSV therefore showed `instrument=255` and
`instrument=0`, obscuring which instruments actually fired.

**Fix:** set `e_on.instrument = m_trackInstrument[t]` (and rename or document
if the handle is ever needed).

---

## 5. Ruled out

- **LOAD_SONG buffer UB** (copy-assigning the non-trivially-copyable
  `EngineState` into unconstructed `new uint8_t[]` memory) — first suspect,
  tested by value-initializing the buffer (`new uint8_t[n]()`): output was
  bit-identical, so the garbage-memory hazard is benign in this run (fresh
  zeroed pages make the destination vector read as a valid empty vector). It
  remains real latent UB — plus a `delete[]`-without-destructor vector leak —
  but it is unrelated to this bug. Cleanest fix: make
  `EngineState::instruments` a fixed `Instrument[128]` array so the whole
  payload is trivially copyable.
- **Sampler voices** — a sampler with no sample data outputs silence through
  every limiter mode used here (confirmed: adding an explicit `!sd` guard
  changed nothing).
- **Effects state** (chorus/delay/reverb buffers, DC blockers, smoothers) —
  zero-input/zero-state paths verified to output zero; LOAD_SONG already
  resets them.
- **Missing samples** — orthogonal; missing sample = silent voice, as
  designed.

---

## 6. Recommended fixes (in order)

1. **Gate note triggering by instrument type** — in `Engine::tickTrack`, do
   not trigger notes for instruments that are not
   `INST_SAMPLER`/`INST_MACROSYN` (or equivalently, return 0 from
   `SynthVoice::renderSample` for `INST_NONE`/`INST_MIDI`/null). This is the
   actual bug fix; it removes all clipping and most of the DC on TEST-FILE.
2. **Fix `--solo` + `--load` ordering** in `m8_render` (§4.1).
3. **Fix `e_on.instrument`** to carry the instrument index (§4.3).
4. **Fix `printTrackInfo`** to reflect post-LOAD state and show `INST_NONE`
   (§4.2).
5. Separately track the MacroSynth fidelity issues: no real
   shape/timbre/color synthesis, and the `sp.volume → amp`-as-drive mapping
   (file volume 0x80 ⇒ 4.5× gain). Even with fix 1, TEST-FILE may still fail
   crest/DC on the sustained MAC drone until these are addressed.

## 7. Repro commands

```
cmake --build build --target m8_render m8_analyze --config Release

build/Release/m8_render.exe --load third_party/m8-files-cxx/examples/songs/TEST-FILE.m8s ^
    --song --seconds 5 --out testfile
build/Release/m8_analyze.exe testfile.wav          # fails 4 hard checks

# demonstrate broken solo (all three byte-identical):
build/Release/m8_render.exe --load ...TEST-FILE.m8s --solo 0 --seconds 2 --out solo0
build/Release/m8_render.exe --load ...TEST-FILE.m8s --solo 2 --seconds 2 --out solo2
build/Release/m8_render.exe --load ...TEST-FILE.m8s --solo 7 --seconds 2 --out solo7
```

---

## 8. Fix applied

Fixes 1–3 from §6 were implemented. Fix 5 (MacroSynth fidelity — real
shape/timbre/color synthesis, and the `sp.volume`-as-drive mapping) is out of
scope here and remains a separate, known limitation (see `ARCHITECTURE.md`
§4, "stored but not yet audible").

### 8.1 `src/engine/SynthVoice.cpp` — the actual bug fix

Added a type gate in `SynthVoice::renderSample`, right after the existing
gate-ramp bookkeeping and before modulator/oscillator processing. Any voice
whose instrument is not `INST_SAMPLER` or `INST_MACROSYN` (i.e.
`INST_NONE`/`INST_MIDI`) now renders silence instead of falling through to the
unconditional, un-enveloped saw oscillator:

```cpp
if (m_instrument) {
    const InstType it = m_instrument->type;
    if (it != InstType::INST_SAMPLER && it != InstType::INST_MACROSYN) return 0.0f;
}
```

This preserves all existing gate/`m_active`/note-on/note-off bookkeeping
(`isActive()` still reports correctly) — it only skips modulator computation
and audio synthesis for types the engine doesn't implement.

### 8.2 `src/engine/Engine.cpp` — event field correctness

`e_on.instrument` now carries the actual instrument index
(`m_trackInstrument[t]`) instead of the sampler's `SampleHandle` (which was
`0` for every non-sampler note and `255` for a sampler with no sample
loaded). This was necessary to see, in the event CSV, which instrument each
NOTE_ON was actually using while diagnosing this bug, and is correct
independent of it.

### 8.3 `src/tools/main_render.cpp` — tooling fixes

- **`--solo` now works when combined with `--load`.** The mute is applied via
  queued `UPDATE_PARAM`/`MIX_TRK_VOL` commands pushed onto the same ring
  *after* `setupEngine()` queues `LOAD_SONG`, instead of mutating
  `engine.getStateForInit().mixer` directly (which `LOAD_SONG` was silently
  overwriting before the first `render()` call).
- **`printTrackInfo` now reflects post-LOAD state.** The call site runs an
  8-frame warm-up render to drain the `LOAD_SONG`/`LOAD_SAMPLE` commands
  before reading `getStateForInit()` back out (`playMode` is still `NONE` at
  that point, so this has no playback side effects).
- **`printTrackInfo` no longer hides unimplemented-type instruments.** It now
  distinguishes a genuinely-empty slot (`INST_NONE` with the untouched
  default name) from an unimplemented type loaded from a song file
  (`INST_NONE` with a real name from the file, e.g. `WAV`, `FM`, `HYPER`),
  and prints the latter as `NONE (unimplemented — silent)` instead of
  skipping it.

### 8.4 Verification

Build: `m8_engine`, `m8_render`, `m8_analyze`, `m8_tests` all rebuilt clean
(Release, MSVC), no new warnings.

Test suite: **294,329 assertions in 96 test cases, all passing** (`m8_tests.exe`
run from repo root — Catch2's discovery of `m8_clone.exe` in
`test_ui_scripts.cpp` is CWD-relative, so run from repo root, not `build/`).

`printTrackInfo` on TEST-FILE now correctly shows the drone sources:

```
  track  instrument  type
  inst 01  WAV           NONE (unimplemented — silent)
  inst 02  MAC           MACROSYN
  inst 03  SAMP          SAMPLER  sample: (none)
  inst 04  FM            NONE (unimplemented — silent)
  inst 05  HYPER         NONE (unimplemented — silent)
  inst 06  MIDI          NONE (unimplemented — silent)
  inst 07  EXT_INST      NONE (unimplemented — silent)
```

`--solo` on TEST-FILE now correctly isolates per track (previously
byte-identical for every track):

```
solo 0: peak 0.000   (inst 01 WAV, NONE — correctly silent)
solo 1: peak 0.953   (inst 02 MAC, MACROSYN — the only real sound source)
solo 2: peak 0.000   (inst 03 SAMP, sample missing — correctly silent)
solo 3–6: peak 0.000 (FM/HYPER/MIDI/EXT_INST — correctly silent)
solo 7: peak 0.000   (no NOTE_ONs)
```

Full-song render, before → after:

| metric      | before (buggy) | after (fixed) |
|-------------|-----------------|----------------|
| clipped     | 637             | **0**          |
| peak        | 1.000           | 0.986          |
| crest       | 2.20 dB         | 4.03 dB        |
| DC L        | 0.0637          | 0.0087         |
| DC R        | -0.0537         | 0.0029         |
| DC worst-1s | 0.1441          | 0.0145         |

`m8_analyze` still fails DC/crest on the residual — this is expected and
matches the §3 experiment exactly: it's the legitimate `MAC` instrument
(inst 02), which has no volume envelope in TEST-FILE (a bare `TrigEnv →
MOD_AMT` mod) and so sustains a raw, undamped saw. That is the separately-
tracked MacroSynth fidelity gap (§6 item 5 / ARCHITECTURE.md §4), not a
regression or a new bug — TEST-FILE is a type-coverage probe file, not a
normal song, and was never expected to pass the audio-quality gate outright.
