# M8 Sampler — Completion Spec

**Audience:** the engineer/agent implementing this. Follow it literally. Where it says
"exact code," type that code. Where it warns about a trap, do not shortcut past it.

**Goal:** finish the M8 **Sampler instrument** so that every field on the on-device
SAMPLER instrument screen is (1) loaded from `.m8s`, (2) rendered, (3) editable in the app,
and (4) **saved back** to `.m8s`. Today most fields load and render and are editable, but
**no instrument edits are ever saved**, and a few fields do not load. Phase 1 fixes that and
is the priority. Phases 2–4 are larger features, specified at a lower level of detail.

---

## 0. Before you touch anything

### 0.1 Read these first
- `AGENTS.md` — build rules, the "hardware-verified constants are sacred" rule, honesty rules.
- `ARCHITECTURE.md` §2 (two-thread model), §"Persistence (src/io/SongIO.*)", §4 feature inventory.
- `status.md` — current state; especially the "Placeholders" and "Not implemented" lists.

### 0.2 Build & test commands (Windows / MSVC, from repo root)
```
cmake --build build --config Release --target m8_tests
build\Release\m8_tests.exe                 # run all tests
build\Release\m8_tests.exe "[io]"          # just the persistence tests
build\Release\m8_tests.exe "[sampler]"     # just the sampler DSP tests
```
Offline WAV render for manual checks:
```
cmake --build build --config Release --target m8_render
build\Release\m8_render.exe --load PATH.m8s --sample-root DIR --note C-4 --instrument 0 --seconds 3 --out out
```
There is a generated sampler probe you can use as a fixture:
`hwtest_out/probes/probe_sampler.m8s` + its sample `hwtest_out/probes/probe_sine.wav`
(regenerate with: `build\Release\m8_makeprobe.exe --type sampler --sample-path /probes/probe_sine.wav --note C-4 --out hwtest_out/probes/probe_sampler.m8s`;
note the sample path argument must start with `/` — if you run makeprobe from Git Bash, prefix the
command with `MSYS_NO_PATHCONV=1` or the path gets mangled).

### 0.3 THE GOLDEN RULE — do not break the byte-identical round-trip
There is a test (tag `[io]`, historically called **L4**) that loads a real `.m8s`, saves it
without edits, and asserts the output bytes are **identical** to the input. This currently
passes **because instruments are never re-serialised on save**. Phase 1 changes that. After
**every** change in Phase 1, run `build\Release\m8_tests.exe "[io]"` and confirm L4 still
passes. If it fails, you changed a byte you should not have — see §1.6.

### 0.4 The map of who does what (so you know where to edit)
- **File format struct** `m8::Sampler` — `third_party/m8-files-cxx/src/synths.hpp` lines 214–231.
  Fields: `number, name, transpose, table_tick, synth_params, sample_path, play_mode, slice,
  start, loop_start, length, degrade`.
- **File format struct** `m8::SynthParams` — same file, lines 100+. Fields you will use:
  `volume, pitch, fine_pitch, filter_type, filter_cutoff, filter_res, amp_type, amp_limit,
  mixer_pan, mixer_dry, mixer_chorus, mixer_delay, mixer_reverb, mods[4], associated_eq`
  (plus `env_*_amt`, `lfo_*_amt` which we do NOT model — leave them alone).
- **Engine struct** `engine::SamplerState` — `src/engine/Engine.h` lines 35–65. This is what the
  audio engine renders and what the UI edits.
- **File → engine (LOAD)**: `convertSongToEngine()` in `src/io/SongIO.cpp`, sampler branch at
  lines ~332–345; helper `libSynthParamsToEngine()` at lines 170–185.
- **Engine → file (SAVE)**: `convertEngineToSong()` in `src/io/SongIO.cpp`, lines 379–452.
  **This function has NO instrument loop** — that is the main bug. Helpers
  `engineSamplerToLibSynthParams()` (line 201) and `engineMacrosynToLibSynthParams()` (line 215)
  exist but are **never called** (dead code). `saveSong()` (line ~527) re-reads the original file,
  calls `convertEngineToSong` to overlay engine state, then `song.write_over(original)`.
- **UI layout** (which fields are editable and their labels): `src/ui/screens/instrument/
  InstrumentSamplerLayout.h`.
- **Render** (what actually makes sound): `src/engine/SynthVoice.cpp` (sampler path uses
  `s.detune` at line ~155), `src/engine/SamplerEngine.{h,cpp}`.

---

## 1. PHASE 1 — Seal load/render/edit/save for the sampler screen (REQUIRED)

### 1.1 Current status of each screen field (the audit you are fixing)

| Field | Loads from file | Renders | Editable | Saves | Action needed |
|---|---|---|---|---|---|
| NAME | yes | (display) | yes | **no** | save (optional, §1.5) |
| SAMPLE path | yes | yes | yes | preserved-if-untouched | none |
| PLAY, START, LOOP ST, LENGTH | yes | yes | yes | **no** | save loop §1.2 |
| DEGRADE, FILTER, CUTOFF, RES | yes | yes | yes | **no** | save loop §1.2 |
| AMP, LIM, PAN, DRY | yes | yes | yes | **no** | save loop §1.2 |
| MFX/CHO, DEL, REV | yes | yes | yes | **no** | save loop §1.2 + relabel §1.4 |
| SLICE (value) | yes | no (stored only) | yes | **no** | save loop §1.2 (render is Phase 2) |
| **DETUNE** | **no (hardcoded 0x80)** | yes | yes | **no** | load fix §1.3 + save loop §1.2 |
| **TRANSP** | **no** | no (round-trip only) | yes | **no** | load fix §1.3 + save loop §1.2 |
| **TBL.TIC** | **no** | no (Tables unimpl.) | yes | **no** | load fix §1.3 + save loop §1.2 |
| **EQ** | no | no (EQ unimpl.) | yes | no | leave for now (§1.7) |

### 1.2 Fix A — add the instrument SAVE loop (the main bug)

**Where:** `src/io/SongIO.cpp`, inside `convertEngineToSong()`. That function currently ends
after the "Effects" block (around line 451) with a closing `}`. Insert the code below **just
before** that closing brace.

**Exact code to insert:**
```cpp
    // Instruments — overlay the fields our engine models onto the ORIGINAL song
    // instruments. We only touch modeled/screen-exposed fields; every other byte
    // (pitch, amp_limit, env_*_amt, lfo_*_amt, mods, associated_eq, sample_path,
    // number) is preserved from the file that was re-read at the start of saveSong().
    // That preservation is what keeps the byte-identical round-trip test passing.
    for (size_t i = 0; i < song.instruments.size() && i < 128; ++i) {
        const auto& engInst = state.instruments[i];

        if (engInst.type == engine::InstType::INST_SAMPLER &&
            std::holds_alternative<m8::Sampler>(song.instruments[i])) {
            auto& smp = std::get<m8::Sampler>(song.instruments[i]);
            const auto& s = engInst.sampler;
            smp.transpose  = (s.transp != 0);
            smp.table_tick = static_cast<uint8_t>(s.tbl_tic);
            smp.play_mode  = static_cast<uint8_t>(s.play);
            smp.slice      = static_cast<uint8_t>(s.slice);
            smp.start      = static_cast<uint8_t>(s.start);
            smp.loop_start = static_cast<uint8_t>(s.loop_st);
            smp.length     = static_cast<uint8_t>(s.length);
            smp.degrade    = static_cast<uint8_t>(s.degrade);
            engineSamplerToLibSynthParams(s, smp.synth_params); // volume/filter/lim/pan/dry/sends
            // DETUNE: engine detune is unsigned with 0x80 == centre; the file's
            // fine_pitch is SIGNED with 0 == centre. See the WARNING in §1.3.
            smp.synth_params.fine_pitch = static_cast<uint8_t>(s.detune - 0x80);
        }
        else if (engInst.type == engine::InstType::INST_MACROSYN &&
                 std::holds_alternative<m8::MacroSynth>(song.instruments[i])) {
            auto& mac = std::get<m8::MacroSynth>(song.instruments[i]);
            const auto& m = engInst.macrosyn;
            mac.transpose  = (m.transp != 0);
            mac.table_tick = static_cast<uint8_t>(m.tbl_tic);
            mac.shape      = static_cast<uint8_t>(m.shape);
            mac.timbre     = static_cast<uint8_t>(m.timbre);
            mac.color      = static_cast<uint8_t>(m.color);
            mac.degrade    = static_cast<uint8_t>(m.degrade);
            mac.reductor   = static_cast<uint8_t>(m.redux);
            engineMacrosynToLibSynthParams(m, mac.synth_params);
        }
        // Any other case (INST_NONE, or engine type != library type): leave the
        // original instrument bytes untouched.
    }
```
Notes:
- This calls the previously-dead helpers `engineSamplerToLibSynthParams` /
  `engineMacrosynToLibSynthParams`. They are correct as written — do not modify them.
- The macrosyn branch is included because it is the same loop and it is needed later for
  Braids; it is low-risk and keeps the two instrument types symmetric.
- `<variant>` and `<type_traits>` are already included by this file (it uses `std::visit`).

### 1.3 Fix B — load the fields that are currently dropped (DETUNE, TRANSP, TBL.TIC)

**Where:** `src/io/SongIO.cpp`, `convertSongToEngine()`, the **sampler** branch (around lines
332–345). It currently contains this line:
```cpp
                s.detune = 0x80;
```
**Replace that single line** with:
```cpp
                s.transp  = inst.transpose ? 1 : 0;
                s.tbl_tic = inst.table_tick;
                // DETUNE: file fine_pitch is signed (0 == centre); engine detune is
                // unsigned (0x80 == centre). Convert with a signed re-centre.
                s.detune  = static_cast<int>(static_cast<int8_t>(inst.synth_params.fine_pitch)) + 0x80;
```

**Where:** same function, the **macrosyn** branch (around lines 346–367). It sets `ms.shape`,
`ms.timbre`, … but never sets `transp`/`tbl_tic`. **Add** these two lines anywhere inside that
branch (e.g. right after `engine::setName(engInst.name, inst.name.c_str());`):
```cpp
                ms.transp  = inst.transpose ? 1 : 0;
                ms.tbl_tic = inst.table_tick;
```

> ### ⚠️ CRITICAL TRAP — DETUNE centre convention. READ THIS.
> Do **NOT** write `s.detune = inst.synth_params.fine_pitch;`. The two use different centres:
> - The **file** `fine_pitch` byte is a **signed** offset: `0x00` = in tune, `0x10` = +1
>   semitone (16 steps × 1/16 semitone), `0xF0` (= −16) = −1 semitone.
> - The **engine** `SamplerState::detune` is **unsigned** with `0x80` = in tune; the renderer
>   computes `(detune − 128) × (1/16 semitone)` (`SynthVoice.cpp:155`).
>
> The correct conversions are therefore:
> - LOAD: `detune = int8_t(fine_pitch) + 0x80`   (fine_pitch 0 → detune 0x80 → in tune ✓)
> - SAVE: `fine_pitch = uint8_t(detune − 0x80)`
>
> If you get this wrong, **every sample plays ~8 semitones off**. There is a test (§1.5,
> S-DET1/S-DET2) whose entire job is to catch this — make it pass, do not weaken it.
>
> Why the current hardcode "worked": `s.detune = 0x80` accidentally equals the in-tune centre,
> so probes with `fine_pitch = 0` sounded correct. It silently ignored any real detune.

### 1.4 Fix C — relabel the first mixer send CHO → MFX (cosmetic, matches firmware 6)

**Where:** `src/ui/screens/instrument/InstrumentSamplerLayout.h`, the `C::CHO` entry (around
line 100). Change the visible label string only:
```cpp
        {C::CHO, {
            {"MFX", 17, 12, "LABEL_DIM", "LABEL_LITE", "label", false, 0},   // was "CHO"
```
Do **not** rename the `CursorId::CHO` enum or any engine field — only the display string.
(Firmware 6 renamed the chorus send slot to "MFX"; it is still the same `mixer_chorus` byte.)

### 1.5 Tests to add (REQUIRED)

Add to `tests/test_persistence.cpp` (tag `[io]`). Follow the existing test style in that file
(there are already load/save round-trip tests to copy from). If a helper to load→edit→save→
reload does not exist, write a small local one using `m8::io::loadSong` / `m8::io::saveSong`.

**S-RT1 — sampler fields round-trip through save/reload.**
1. `loadSong("hwtest_out/probes/probe_sampler.m8s", "hwtest_out")` → `LoadResult a`. Require `a.ok`.
2. Take `a.sequencer` / `a.state`, and in `a.state.instruments[0].sampler` set distinctive
   values: `play=2, start=0x11, loop_st=0x22, length=0x33, slice=0x44, degrade=0x55, amp=0x66,
   filter_type=1, cutoff=0x77, res=0x18, lim=1, pan=0x40, dry=0x50, cho=0x10, del=0x20, rev=0x30,
   detune=0x90, transp=0, tbl_tic=0x0F`.
3. `saveSong("<temp>.m8s", a, a.sequencer, a.state, err)`. Require it returns true.
4. `loadSong("<temp>.m8s", "hwtest_out")` → `LoadResult b`.
5. Assert **every** field set in step 2 equals the same field in `b.state.instruments[0].sampler`.
   (This is what proves save works. `detune` round-tripping to `0x90` also proves the
   fine_pitch centre conversion is consistent both directions.)

**S-RT2 — byte-identical round-trip still holds.** Run the existing L4 test. It must still pass.
If your suite doesn't already have it, add: load a real fixture `.m8s`, `saveSong` with **no**
edits, assert output bytes == input bytes.

**S-DET1 — detune changes rendered pitch by the right amount (render-level).**
Build an engine with a sampler pointed at a pure-sine sample at 261.63 Hz. Render one C-4 note
with `detune = 0x80` and assert the measured fundamental is ~261.63 Hz (within 20 cents). Then
render with `detune = 0x80 + 16` and assert the fundamental is ~277.18 Hz (C#4, +1 semitone,
within 20 cents). Use `AudioMetrics`/the FFT pitch helper already used by `[audio]` tests. This
locks the sign AND the scale of the detune mapping.

**S-DET2 — detune loads from the file (unit-level).**
Construct a `.m8s` (or extend `m8_makeprobe` with a `--detune` option) whose sampler has
`synth_params.fine_pitch = 0x10`. `loadSong` it and assert `state.instruments[0].sampler.detune
== 0x90`. And one with `fine_pitch = 0xF0` → assert `detune == 0x70`.

### 1.6 If the byte-identical test (L4) breaks after your changes
That means a byte you re-serialised differs from the original. Debug procedure:
1. It is almost always a field the SAVE loop writes but the LOAD path does **not** populate, so
   the engine holds a default that differs from the file. You already fixed the known ones
   (`transp`, `tbl_tic`, `detune`) in §1.3 — verify you actually did both directions.
2. Use `m8_analyze --diff` on nothing here; instead compare the two `.m8s` byte arrays in the
   test and print the first differing offset. Map that offset back to a field via the read
   order in `Sampler::from_reader` (`third_party/m8-files-cxx/src/synths.cpp`).
3. Do **not** "fix" it by skipping the round-trip test. Fix the load/save symmetry.

### 1.7 Explicitly NOT done in Phase 1 (leave as-is, note in status.md)
- **EQ** field: not mapped. Per-instrument EQ is unimplemented engine-wide; leaving `s.eq`
  at its default and not writing `associated_eq` is correct for now. (For V4.1 files EQ lives
  in a separate section, not in the instrument.)
- **NAME save** (optional add-on): to also persist the instrument name, in the sampler branch
  of the save loop set `smp.name` from `engInst.name` (trim trailing spaces; `m8::Sampler::write`
  pads to the fixed width). Add a field to S-RT1 asserting the name survives. Do this only if
  L4 still passes afterward — name width/padding is the most likely thing to perturb bytes.
- **synth_params.pitch** (coarse instrument pitch): there is no engine field for it and the
  sampler screen does not expose it. Preserve it (the save loop already leaves it untouched);
  do not try to apply it.

### 1.8 Phase 1 acceptance criteria (all must hold)
1. `build\Release\m8_tests.exe` — the full suite passes (no regressions).
2. `[io]` tests pass, including the existing byte-identical round-trip and the new S-RT1/S-RT2.
3. `[sampler]` / `[audio]` tests pass, including new S-DET1.
4. Manual: load `probe_sampler.m8s` in the app, change AMP/PAN/DETUNE/PLAY, Save, reload the
   file — the changes are still there.
5. `status.md` updated: move the sampler load/save items out of "Placeholders/Not implemented",
   and record the DETUNE centre-conversion fact next to the other hardware-verified constants.

---

## 2. PHASE 2 — SLICE playback + REPITCH / BPM play modes (feature, medium size)

Today the sampler renders PLAY modes `00`–`08` (FWD/REV/loops/ping-pong/osc-region — see
`SamplerEngine`). It does **not** implement:
- **SLICE** (the `slice` byte + the slice table): the value loads and saves (after Phase 1) but
  playback ignores it. A sliced sampler should start playback at the selected slice's offset.
- **PLAY modes `09`–`0E`** (`REPITCH`, `REP.REV`, `REP.PP`, `REP.BPM`, `BPM.REV`, `BPM.PP`):
  currently fall back to a simpler mode.

**Requirements:**
1. Add a slice table to the sampler instrument data path (the M8 stores slice points; confirm
   how `m8-files-cxx` exposes them — check `Sampler` and the song's slice storage; if slices are
   not parsed yet, that parsing is a prerequisite and must be added in the library submodule).
2. In `SamplerEngine`, when `slice != OFF`, compute the start/end sample offsets from the chosen
   slice instead of `start`/`length`.
3. Implement `REPITCH` (play the whole sample repitched to the note, ignoring SR-correction the
   way FWD does) and `BPM` (time-stretch/loop to song tempo) variants. Consult the M8 operation
   manual (in `manual/` locally) for exact semantics of each mode.
4. Tests: for each new PLAY mode, render a known sample and assert the expected duration/pitch
   relationship; a slice test asserting playback starts at the slice offset.

**Prerequisite / risk:** slice data may not round-trip through the library yet — verify and, if
needed, extend `m8-files-cxx` first (and keep its byte-identical round-trip intact there too).

---

## 3. PHASE 3 — Sample browser/preview, REC (record), and the sample EDIT screen (large)

These are the `SAMPLE ... LOAD / REC` actions and the `EDIT` sample-editor screen. `status.md`
lists "sample browser/preview, live recording, .m8i save" as **not implemented**. This is a UI
+ IO feature, not a small mapping fix — it needs its own design pass. Scope, roughly:
- **LOAD** already works (file browser assigns `sample_path`).
- **Sample preview**: play the selected sample without committing it.
- **REC**: capture audio from an input into a new sample buffer + write a WAV. Requires an
  audio *input* path (the engine currently only outputs); this is the biggest piece.
- **EDIT**: a waveform view with start/loop/length handles.
Treat each as a separate deliverable with its own spec; do not attempt in one pass.

---

## 4. PHASE 4 — DSP fidelity polish (small, optional)

Per `status.md` "Placeholders":
- **FILTER `06`/`07` (ZDF LP/HP)** currently alias to the simpler SVF forms. Implement the
  zero-delay-feedback variants.
- **LIM `04`–`08` (POST / POST:AD / POST:W1..W3)** alias to simpler curves. Implement the real
  post/waveshaper limiter modes.
These are audible-fidelity refinements; gate them with `[audio]` spectral tests comparing
against the intended transfer function (math reference), not against hardware captures.

---

## 5. Order of work
1. **Phase 1** (this is the ask — do it first, in full, with tests). Small, mechanical, high value.
2. Phase 2 (SLICE + play modes) — the next functional gap.
3. Phase 4 (filter/lim fidelity) — quick wins, do opportunistically.
4. Phase 3 (record/edit) — largest; separate spec.

Phase 1 alone "seals" the sampler *instrument screen*: every field on it will load, render
(where the engine models it), be editable, and **save**. Phases 2–4 complete sampler *behavior*
that the screen implies but the engine does not yet fully perform.

---

## 6. FINAL REPORT — required deliverable (do this when Phase 1 is done)

When you finish Phase 1, produce a written report with the following sections **in this order**.
Do not summarise in place of evidence — paste the actual snippets and the actual command output.

### 6.1 Changes made
For **each** of Fix A (§1.2), Fix B (§1.3), Fix C (§1.4), and each new test (§1.5), show:
- the file path and the function/area changed, and
- the exact `git diff` for that change (paste the diff hunk, not a description).
If you deviated from the spec's code in any way, show the deviation and explain why in one sentence.

### 6.2 Test results — paste the real console output
Run and paste the **full output** (including the final pass/fail summary line) of each:
```
build\Release\m8_tests.exe                 # whole suite — paste the summary line
build\Release\m8_tests.exe "[io]"          # persistence, incl. byte-identical round-trip + S-RT1/S-RT2
build\Release\m8_tests.exe "[sampler]"
build\Release\m8_tests.exe "[audio]"       # incl. S-DET1
```
Then, explicitly, one line each confirming:
- Byte-identical round-trip (L4): **PASS** — paste the assertion/line proving it.
- S-RT1 (fields survive save→reload): **PASS**.
- S-DET1 (detune → pitch: 0x80 → C-4, 0x90 → C#4): **PASS** — include the two measured
  fundamentals in Hz.
- S-DET2 (fine_pitch 0x10 → detune 0x90; 0xF0 → 0x70): **PASS**.
- Total test count before vs after (e.g. "96 → 100 cases").

### 6.3 Manual verification (the app)
Show the load→edit→save→reload check from §1.8(4): state which fields you changed, that you
saved, reloaded the file, and that the values persisted. If you can capture a screen dump or the
`m8_nav --dump-screen` / `dump_json` of the instrument screen before and after, include it.

### 6.4 Byte-diff proof (only if L4 needed debugging)
If the byte-identical test failed at any point, show the first differing offset you found, the
field it mapped to, and the load/save symmetry fix you applied (§1.6). If it never failed, say so.

### 6.5 Status
- Paste the `status.md` diff moving the sampler load/save items out of "Placeholders / Not
  implemented" and recording the DETUNE centre-conversion constant.
- List anything from §1.7 you intentionally left out (EQ, NAME-save if skipped, coarse pitch).

### 6.6 Regressions / risks
State plainly whether any previously-passing test now fails (there must be none), and flag any
behaviour change to existing songs (e.g. songs that had a non-zero `fine_pitch` will now detune
correctly where before they played at centre — this is the intended fix, but call it out).
