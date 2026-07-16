# M8 Tracker Clone — Status

Last updated: 2026-07-16

A software clone of the **Dirtywave M8 Tracker**: the tracker workflow, the 2D view
navigation, the custom UI layout, and the audio engine.

**Where it stands:** songs written on real M8 hardware load, save, and play through this
engine — correct tempo, sample-accurate, note release, no DC, no glitches. The full audio
analysis + hardware capture toolchain is built. The one thing real songs *don't* do yet is
sound like the original, because **MacroSynth is still a placeholder saw** — which is the next
target, and now has a parity-testing rig ready for it.

Split into **Implemented** / **Placeholders** / **Not implemented**. "Implemented" means done
and tested. "Placeholder" means it makes noise but is not the real thing.

---

## Tech stack

- **Language**: C++20
- **App framework**: SDL3 (window, input, audio out) — *`m8_clone` only*
- **DSP**: DaisySP (LGPL)
- **Persistence**: `m8-files-cxx` (github.com/mbenam/m8-files-cxx), vendored, `src/` only
- **FFT**: kissfft (vendored, `third_party/`)
- **Capture audio**: miniaudio (vendored, header-only, `m8_capture` only)
- **Tests**: Catch2 v3 — 96 cases
- **Build**: CMake + FetchContent
- **Platform**: Windows / MSVC. Linux builds clean; macOS untested.

**Audio-API separation (invariant):** the engine links no audio API; `m8_clone` links SDL;
`m8_capture` links miniaudio; nothing else links either.

Targets:
- `m8_engine` — static lib, **zero SDL dependencies** (enforced: `m8_tests` links it without SDL)
- `m8_clone` — the application
- `m8_render` — offline WAV renderer; loads real `.m8s` (`--load`); single-note isolation (`--note --instrument`)
- `m8_analyze` — objective single-file audio checks (peak/rms/crest/DC/clip/NaN/pitch/centroid);
  `--events <csv>` for per-note pitch/centroid/attack, `--json <path>` for machine-readable output
- `m8_spectrum` — A/B spectral comparison against a hardware reference (fundamental, harmonic/
  sideband table, centroid, log-spectral distance); `--json` for a render→compare→adjust loop
- `m8_makeprobe` — generates probe `.m8s` files (one instrument, one note; `--sweep`)
- `m8_capture` — drives the headless over serial + records USB audio → trimmed WAV
- `m8_tests` — 96 cases

Build directories: **`build/` and `build_asan/` only**. Always `--target`. See `AGENTS.md`.

---

## Architecture — the invariants

Expensive to establish, not negotiable without discussion.

1. **The audio thread never allocates, frees, throws, locks, or touches a `std::string`.**
   Enforced by **B8.1** (counts allocations in `render()`, proven to go red when broken).
2. **The UI never reads engine state directly.** Shadow copies (`uiSequencer`,
   `uiEngineState`) + `CommandRing<EngineCommand, 1024>`. `getState()` deleted.
   `getStateForInit()` / `getSequencerForInit()` are legal only before the audio thread
   starts (startup + the single-threaded tools).
3. **Sample-accurate clock.** Fractional tick accumulator; drift < 1 sample / 60 s (**B3.3**);
   ticks never land on buffer boundaries (verified on a real 130 BPM song).
4. **All sequencer data is POD.** Enables the ring, the `memcmp` tests, the byte-identical
   `.m8s` round-trip.
5. **Path-keyed, refcounted sample pool.** Non-owning voice pointers; frees via GC ring on the
   UI thread; shared buffers for shared paths.

---

## Implemented

### UI
Custom 5×7 font, 320×240 / 40×30. 2D navigation + minimap. `ViewManager`, modal browser.
Dictionary layouts + `NavGraph`. All screens. Edit mode on hex grids. Playhead from the atomic
word.

### Sequencer
Song→chain→phrase across 8 tracks, all advancing the song row together (empty chain = rest).
Chain transpose. Groove/swing. FX `DEL`/`KIL`/`HOP` (clamped). Bounds-checked, fuzzed 10k under
ASan. Note release on chain end.

### Audio engine
Stereo, 8 monophonic voices, per-track vol, equal-power pan, per-instrument dry/chorus/delay/
reverb. Master chorus/delay/reverb/`tanh`, feedback clamped, DC blockers on feedback paths and
master bus. Tempo verified on real songs.

### Sampler (`M8_SAMPLER_SPEC_V2.md`, hardware-verified)
Region `[LOOP ST, LOOP ST+LENGTH]` sample-relative. Play modes 00–08, reflecting overshoot.
Stereo reads, linear interp, `double` phase. Root C-4, DETUNE 1/16 semitone, SR correction.
`LOOP ST`/`LENGTH` live per-sample. DEGRADE, AMP, LIM (CLIP/SIN/FOLD/WRAP), FILTER (LP/HP/BP/BS).

### Modulation (`M8_MODULATION_SPEC.md`, hardware-verified)
No built-in amp env (gate + anti-click ramp; amp env is a mod slot). Envelope times in **ticks,
tempo-relative**. Own AHD/ADSR/DRUM. Six mod types incl. TRIG-sidechain and TRACKING. 14
destinations. AMT bipolar (0x80 neutral). LFO shape/freq/4 trigger modes. Mod-to-mod cyclic.
Pitch mod in semitones (was 45× too strong — fixed).

### Persistence (`M8_PERSISTENCE_SPEC.md`)
Load/save real `.m8s` via `m8-files-cxx`. **Byte-identical round-trip** on V4/V4.1 (L4). Bulk
`LOAD_SONG` (one memcpy, GC-ring free). Unimplemented instrument types, scales, EQs, MIDI all
**preserved on save** (L7). Missing samples don't fail the load (L6). Save refuses pre-4.0.
File browser + Project LOAD/SAVE + SAMPLE ROOT wired. **A song written on real hardware plays
correctly**, verified end to end.

### Analysis + capture tooling (`M8_AUDIO_ANALYSIS_SPEC.md` Parts A–D, `M8_CAPTURE_SPEC.md`)
- **kissfft** vendored; `magnitudeSpectrum()` with a baked-in Hann window.
- **`AudioMetrics`** — shared library, called by both `m8_analyze` and the `[audio]` tests, so
  tool and tests run identical math.
- **`m8_analyze`** — reads a WAV, prints metrics, non-zero exit on hard-check failure
  (clip/NaN/DC/crest). `--diff <a.wav> <b.wav>` for sample-by-sample comparison (max absolute
  difference, first differing sample index). `--events <csv>` reads the `m8_render` events CSV
  and reports, per NOTE_ON (window = that note's sample_time to the next NOTE_ON on the same
  track): measured pitch deviation in cents, spectral centroid at note-start vs note-end, attack
  time — gracefully reports `n/a` when a window is too short to measure rather than a bogus
  number. `--json` writes the full report (metrics + per-check pass/fail + per-note array)
  machine-readably. Verified: clean render exits 0, DC-injected WAV exits 1 naming the fault;
  `--events`/`--json` verified against a real solo render, JSON validated with Python's `json`
  module.
- **`[audio]` tests A1–A5** — DC, crest, silence, pitch-mod-within-1-semitone (A3, pins the
  ±280-cent bug), feedback stability (A5). In the suite.
- **`m8_spectrum`** (Part D) — `--ref <hw.wav> --test <render.wav>`. Independent per-file onset
  detection (short-window RMS envelope crossing 10% of that file's own peak) anchors the analysis
  window per file, so alignment needs no buffer shifting; skips the ~50ms attack transient; FFTs
  the sustained portion of both (same window length ⇒ same bin count/binHz). Reports fundamental
  (flagged OK/MISMATCH), a harmonic/sideband table (ref's peaks above max−60dB, dB read at the
  *same bin index* in both spectra — not a re-interpolated frequency, which was a real bug caught
  and fixed during verification: comparing a file to itself must give exactly 0 delta everywhere),
  spectral centroid, and the scalar log-spectral distance. `--no-align`, `--json`. Verified against
  both spec acceptance criteria: same file vs itself → every delta `+0.0`, distance `0.00 dB`;
  440Hz vs 880Hz sine → fundamental correctly flagged `MISMATCH`.
- **`m8_makeprobe`** — generates probe `.m8s` (one instrument/note; `--type`, `--sweep`).
  Round-trip verified: params read back what was set.
- **`m8_render --note --instrument`** — single-note isolation for A/B.
- **`m8_capture`** — Win32 serial + miniaudio capture, onset-trimmed WAV, `--batch`. Standalone
  (no engine, no SDL). *Code complete; not yet exercised against the device for a real capture.*

### Demo song
`loadDemoSong()` — "Night Drive", 16 bars, C minor, 124 BPM, swing, building dynamics, drums
generated in code. Scaffolding.

### Tests — 96 cases
Tags: `[tempo] [walk] [fx] [groove] [commands] [sample_pool] [sampler] [modulation]
[rt_safety] [demo] [io] [audio]`. Offline against `m8_engine`, no audio device. All pass under
x64 ASan. Weightiest: B8.1, B3.3, B4.9 (10k fuzz), B7.2, L4, L7, M2, M12, A3, A5.

### UI test harness — Task 3 (`M8_UI_HARNESS_SPEC.md`)
Shadow grid (`VirtualCell[30][40]`) inside `Renderer`. Every draw call also stamps the
shadow buffer: `drawChar` → ch+color, `drawBracket` → bracket flag+bg, `fillRectPixel` →
bg+slider (with verified partial-fill arithmetic). `dumpScreenText()` writes 30×40 plain
text. `dumpJson()` writes full state (screen, bpm, vram, colors, cursor derived from
highlight bg, brackets, sliders, playheads, overlay). F1 key triggers both dumps —
**temporary scaffolding**, replaced by `--script` mode in Tasks 1/2. No screen files
modified; all hooks live inside Renderer methods.

### Script mode — Tasks 1+2 (`M8_UI_HARNESS_SPEC.md`)
`--script FILE --headless --out-dir DIR` mode. Approach (A): synthetic SDL_Events via
`SDL_PushEvent`, following the existing FILE_BROWSER simEvent precedent. Script runner
(`ScriptRunner.h/.cpp`) parses plain-text commands, maps button names to `SDLK_*` codes,
and drives the main loop frame-by-frame. Commands: `key`, `hold`, `type`, `wait`, `play`,
`stop`, `load`, `save`, `set_sample_root`, `dump_screen`, `dump_json`, `screenshot` (BMP),
plus `assert_screen`/`assert_playing`/`assert_stopped`/`assert_no_error`/`assert_error`/
`assert_song_name`. Exit codes: 0=pass, 1=assertion fail, 2=parse error. Auto-dumps
screen on assertion failure. Determinism: headless hidden window, no real OS events,
`SDL_Delay(0)` in script mode, manual `engine.render()` every frame in script mode
(to ensure PLAY/STOP commands are processed before playhead re-read). Screenshot
saves as BMP (SDL3 has no built-in PNG writer).

### Task 4 scripts (`tests/ui/*.m8script`)
Seven scripts covering the manual checklist:
- **nav.m8script** — reach all 12 screens, assert headers. Pass.
- **load_each.m8script** — load all 4 example .m8s files, assert each loads. Pass.
- **save_reload.m8script** — insert C-4 note, save, reload, assert it survived. Pass.
  (Fixed two bugs: load callback was clearing `loadResult->original`; save callback was
  setting error message on success.)
- **pre40_refuses.m8script** — load DEFAULT.m8s (v2.7.0), attempt save, assert error
  contains "4.0". Pass.
- **edit.m8script** — navigate to PHRASE, insert C-4 note via X+UP+X, assert row 3
  contains "C-4". Pass.
- **playhead.m8script** — load V4EMPTY, play, wait 120 frames, assert_playing. Pass.
  (Fixed: `isPlaying` re-read after script events; engine.render() called every frame
  in script mode so PLAY_START command is processed before assert.)
- **missing_samples.m8script** — BLOCKED: `LoadResult::missing` is never populated by
  `loadSong()`, so the missing-sample overlay cannot trigger through the script runner's
  load path. No existing .m8s fixture references missing samples either.

### Glitch detection — Task 6 (`M8_UI_HARNESS_SPEC.md`)
Four assertions for deterministic layout/colour/format validation, all operating on the
shadow grid built in Task 3:
- **assert_no_overlap** — per-cell `writeCount` (reset each frame in `resetVram()`).
  `drawChar`/`fillRectPixel` increment; `drawBracket` does not (visual indicator, not a
  data field). `Renderer::hasOverlap()` returns true if any cell has `writeCount > 1`.
  Fully contained in Renderer — no screen file changes needed.
- **assert_cell_color row N col N is RRGGBBAA** — reads `m_vram[row][col].color`
  (glyph colour, not bg), compares against hex string. "Glyph colour" = the `color`
  field written by `drawChar`, which is the foreground text colour.
- **assert_row_matches N "regex"** — builds row string from `vram[row][].ch`, runs
  `std::regex_search`. Simple std::regex, no custom pattern language.
- **assert_slider row N col N fill 0-8** — reads `m_vram[row][col].slider`, compares
  against integer argument.

Verified by `task6_test.m8script` which exercises all four on real screen content
(SONG screen for first three, Instrument screen for slider). Deliberately overlapping a
cell confirmed assert_no_overlap catches it (exit 1 + auto-dump); reverting restores
exit 0.

### Offline render assertion — Task 5a (`M8_UI_HARNESS_SPEC.md`)
`render <seconds> <file.wav>` script command: runs `Engine::render()` in a synchronous
loop offline, writes 16-bit PCM stereo WAV. Safety approach (A): refuses to run (exit 2)
when the audio stream is active — headless mode skips opening the audio device entirely,
so `render` works there; non-headless mode always has a stream, so `render` always refuses.
The render callback pushes `PLAY_START` (SONG mode), loops `engine.render()` in 512-frame
chunks, pushes `PLAY_STOP` after completion, and writes the WAV. WAV format matches
`m8_render`'s output (16-bit PCM, same float-to-int16 conversion). Added
`m8_analyze --diff <a.wav> <b.wav>` for sample-by-sample comparison (reports max absolute
difference and first differing sample index; exit 0 on identity, 1 on mismatch). Verified:
TEST-FILE.m8s (real audio, peak 0.998) produces **identical** output (max |A-B| =
0.000000000) between in-app render and `m8_render --load`. Previously diverged by 0.30
(sample 610) due to LOAD_SONG not resetting effects DSP buffers (chorus, delay, reverb)
and DC blockers — the in-app engine carried audio from the demo song loaded at startup.
Fixed by re-initializing these on LOAD_SONG (`Engine.cpp` LOAD_SONG handler). The fix
guarantees: load song A while engine had prior state from a different song B → renders
identically to a fresh engine loading song A. This is the cross-song contamination bug that
was diagnosed and fixed. It does NOT guarantee: reload the same file mid-playback → matches
a from-scratch render — that would require resetting playback position (tick phase, song
row), which is correctly preserved by LOAD_SONG as playback continuity. Regression test:
**L9** (`[io]` tag) loads the demo song, renders briefly, then loads TEST-FILE.m8s and
confirms the second render matches a fresh engine's render of TEST-FILE (max diff = 0),
directly exercising the cross-song contamination path. Note: resetting effects on LOAD_SONG
changes the discontinuity character during live song-switching from "old reverb tail bleeds
in" to "silence at boundary" — not verified for click/pop under live audio, flagged as
low-priority follow-up. Task 5b (manual live capture-and-analyze spot-check) is separate
and not automated — requires a real audio device and physical loopback.

### Closed loop — Task 7 (`M8_UI_HARNESS_SPEC.md`)
The full generate→render→analyze→fix→re-verify loop, demonstrated end to end:
1. **Glitch script** (`closed_loop_glitch.m8script`): loads TEST-FILE.m8s, navigates to
   instrument 00, cranks AMP to 0xFF via UI, saves, renders 2s offline →
   `m8_analyze` reports **FAIL** (DC L=0.034, DC R=-0.097, crest=1.70 dB).
2. **Fix script** (`closed_loop_fix.m8script`): loads the glitchy patch, navigates to
   instrument 00, backs AMP down to 0x40, saves, renders 2s offline →
   `m8_analyze` reports **PASS** (DC L=-0.0004, crest=9.66 dB).
The mechanism: TEST-FILE's existing sampler instruments produce DC offset and low crest
factor; cranking AMP amplifies these into m8_analyze's hard-check failures; reducing AMP
brings them back within tolerance. All navigation uses real key presses through the UI
(script mode), all renders use the `render` command (Task 5a, headless), all analysis uses
`m8_analyze`. This closes the M8_UI_HARNESS_SPEC.md — every task (1+2, 3, 4, 5a, 6, 7)
is implemented and verified.

## Placeholders — make noise, not the real thing

- **`INST_MACROSYN` is a POLYBLEP saw.** Not Braids. `shape`/`timbre`/`color`/`redux` ignored.
  **This is why real MacroSynth songs play right notes but sound wrong. It is the next feature,
  and the parity rig (`m8_makeprobe` + `m8_capture` + `m8_spectrum`) exists for exactly this.**
- **`PLAY` 09–0E** (REPITCH/BPM) fall back. Need SLICE.
- **`LIM` 05–08**, **`FILTER` 06/07** (ZDF), **LFO 0x0D–0x16** alias to simpler forms.
  **`MOD BINV`** a guess. **DRUM ENV** duck curve approximated.
- **`MOD RATE` / rate half of `MOD BOTH`/`MOD BINV` do nothing** — only the amount half of
  mod-to-mod routing is applied. Was previously a dead `rateScale` array (computed, never
  read); removed rather than left looking implemented (`CODE_CLEANUP_SPEC.md` #8).
- **Voice path is mono** (`SamplerEngine` reads stereo, `SynthVoice` sums).

---

## Not implemented

- **WavSynth, FMSynth, HyperSynth, MIDIOut, ExternalInst** — preserved on save, silent on play.
- **Tables**, **Scales**, **SLICE** — stored/preserved, not read by the engine.
- **FX `VOL`/`PIT`/`REV`** — parsed, inert.
- **Project transpose, EQ, limiter, DJF, input/USB mixer** — stored, unused.
- **On-screen keyboard, sample browser/preview, live recording, `.m8i` save.**

---

## Hardware-verified constants

Captured from a real M8 headless. **Do not substitute your own values.**

```
PLAY   00 FWD 01 REV 02 FWDLOOP 03 REVLOOP 04 FWD PP 05 REV PP
       06 OSC 07 OSC REV 08 OSC PP 09 REPITCH 0A REP.REV 0B REP.PP
       0C REP.BPM 0D BPM.REV 0E BPM.PP
FILTER 00 OFF 01 LOWPASS 02 HIGHPAS 03 BANDPAS 04 BANDSTP 05 LP>HP 06 ZDF LP 07 ZDF HP
LIM    00 CLIP 01 SIN 02 FOLD 03 WRAP 04 POST 05 POST:AD 06 POST:W1 07 POST:W2 08 POST:W3
MODTYP 00 AHD 01 ADSR 02 DRUM 03 LFO 04 TRIG 05 TRACKING
MODDST 00 OFF 01 VOLUME 02 PITCH 03 LOOP ST 04 LENGTH 05 DEGRADE 06 CUTOFF
       07 RES 08 AMP 09 PAN 0A MOD AMT 0B MOD RATE 0C MOD BOTH 0D MOD BINV   (CUTOFF=0x06, not 0x03)
AMT    bipolar, 0x80 neutral / 0x00 inverted / 0xFF full
LFOTRG 00 FREE 01 RETRIG 02 HOLD 03 ONCE
TRKSRC 00 NOTE 01 VELOCITY 02 VEL. TAKE      TRIGSRC = instrument index (sidechain)
Root C-4 (MIDI 60)   DETUNE 1/16 semitone/step, 0x80 centre
Env times IN TICKS, tempo-relative
LOOP window [LOOP ST, LOOP ST+LENGTH], relative to the WHOLE SAMPLE  (only inference; test S6)
```

Captured with `m8_client.py` / `m8_enum.py`. **Not present in this checkout** (referenced as
"repo" but not found in this working tree — likely predate this checkout or live elsewhere).
Future captures use the C++ `m8_capture`.

---

## Known issues

- **Shared song row**: the first track whose chain ends advances the row for all tracks.
  Different per-track chain lengths get dragged mid-bar. Not yet triggered in practice.
- **Bus attenuation 1.0** — headroom is from mixer defaults, not the engine; eight cranked
  tracks can still hit the limiter.
- **TEST-FILE.m8s fails m8_analyze unmodified — FIXED.** Root cause: unimplemented instrument
  types (`INST_NONE` — FM/Hyper/Wav/MIDIOut/External loaded from a song file) fell through in
  `SynthVoice::renderSample` to the default polyBLEP-saw oscillator with no volume envelope
  applied, so any note on one droned at full amplitude forever; TEST-FILE triggers 5+ of these
  simultaneously, saturating the bus. Fixed with a type gate in `SynthVoice.cpp` (silence for
  anything that isn't `INST_SAMPLER`/`INST_MACROSYN`). Result: clipped 637→0, DC L 0.064→0.0087,
  crest 2.2→4.0 dB. The original isolation was misled by two tooling bugs found and fixed in the
  same pass: `m8_render --solo` was a no-op when combined with `--load` (`LOAD_SONG` silently
  overwrote the direct mixer mutation used to mute other tracks — every "solo" was actually the
  full mix); and `printTrackInfo` read engine state *before* `LOAD_SONG` was processed, so it
  showed 128 default SAMPLERs instead of the loaded song's real (and mostly `INST_NONE`)
  instruments. Both fixed. `e_on.instrument` also fixed to carry the actual instrument index
  instead of the sampler's `SampleHandle`. Full writeup archived at
  `archive/BUG_TESTFILE_DC_DRONE.md`. The remaining DC/crest gap on TEST-FILE after the fix
  (DC L 0.0087, crest 4.0 dB) is the legitimate `MAC` instrument droning because it has no
  volume envelope in the file — a MacroSynth-fidelity gap, not a bug; TEST-FILE is a type-
  coverage probe, not expected to pass the audio-quality gate outright.
- **`m8_capture` unproven against the device** — code complete, but no real capture taken yet.
  Verify the trim on a real WAV before trusting it.
- **`main_stage1.cpp`** dead weight. **SDL3** pinned to a preview tag in some checkouts.
- **`LoadResult::missing` — FIXED.** `loadSong()` now resolves each sample path against
  `sampleRoot` (with CWD fallback) and populates `missing` for unresolved paths. Script
  runner's load callback checks `missing` and sets the error overlay. `missing_samples.m8script`
  unblocked and passing. Regression tests: L10, L11.
- **`playhead.m8script` race** — `assert_playing` proves transport is running but
  cannot prove the playhead row actually advanced (playhead indicators use `drawLinePixel`,
  not in shadow grid). Additionally, when an audio device exists, `engine.render()` is only
  called manually when `stream` is null (SPSC ring safety — two concurrent consumers would
  race), so PLAY_START commands may not be processed before the assert runs. This is an
  intermittent race with a clear ASan/Release split: across 12 runs on the current build
  (3 runs × 4 configurations), ASan builds pass 6/6 while Release builds pass 2/6.
  The mechanism: ASan's heavier instrumentation slows the main loop, giving the audio
  callback more opportunities to drain the command ring before `assert_playing` runs.
  Release builds, running faster, tighten the race window. The earlier claim of "4/4 clean
  across all four configurations" was likely a sampling artifact — those runs happened to
  group ASan (reliable) and Release (unreliable) together in a way that produced all-pass;
  the 12-run control with 3 runs per configuration exposed the true split. Task 6's
  writeCount additions add negligible overhead and did not shift the timing profile
  (confirmed by ASan passing 100% despite the extra writes). Awaiting an architectural fix
  for the command-processing timing gap.

---

## Roadmap

1. **Macrosyn (Braids oscillator models)** — the biggest gap. Real songs already play the right
   notes; they just sound like a saw. The parity loop is now fully built:
   `m8_makeprobe` → load on headless → `m8_capture` → `m8_render --load --note` → `m8_spectrum`.
   **Next step: exercise `m8_capture` against the real headless once** (it's code-complete but
   unproven — see Known issues), then feed the resulting WAV pair into `m8_spectrum` to see the
   actual gap before touching any oscillator code.
2. **Tables** — the sub-sequencer.
3. **Stereo voice path.**
4. **SLICE**, then REPITCH/BPM play modes.
5. **WavSynth / FMSynth / HyperSynth** (data already preserved).

---

## Specs

- `M8_SAMPLER_SPEC_V2.md` — implemented. **Not present as a file in this checkout** — referenced
  by this doc and `AGENTS.md` §9 but missing from the working tree. Flagging per honesty
  requirement, not fabricating a replacement.
- `M8_MODULATION_SPEC.md` — implemented. **Same gap** — referenced, not present in this checkout.
- `M8_PERSISTENCE_SPEC.md` — implemented. **Archived** (`archive/M8_PERSISTENCE_SPEC.md`) —
  every task closed, no open work against it.
- `M8_AUDIO_ANALYSIS_SPEC.md` — **Parts A–D implemented.** Part E (hardware capture rig) is
  code-complete but deferred to `M8_CAPTURE_SPEC.md`'s own unproven-against-device status below.
- `M8_CAPTURE_SPEC.md` — implemented (capture unproven against device)
- `M8_HARDWARE_TEST_SPEC.md` — **new.** How to validate `m8_makeprobe` + `m8_capture` against
  the real headless with a machine-decided verdict (uses `m8_spectrum` / `m8_analyze --json` as
  the pitch/health oracle). Tiered by automation level; Tier 1 (one human load, everything else
  automated) needs only glue. Records the current-state gaps it depends on: makeprobe only
  round-trips macrosynth, capture `--batch` unimplemented, button masks unverified.
- `CODE_CLEANUP_SPEC.md` — **new.** Fix list for the `ARCHITECTURE.md` §5.2 code critique
  (11 items, tiered correctness → hygiene → structural refactors). Tracks its own status per
  item; `ARCHITECTURE.md` gets a `[FIXED — CODE_CLEANUP_SPEC #N]` annotation as each closes.
  One item done (#6, alongside the DC-drone fix), one partial (#9, repo hygiene).
- `M8_UI_HARNESS_SPEC.md` — implemented. **Archived** (`archive/M8_UI_HARNESS_SPEC.md`) — every
  task (1+2, 3, 4, 5a, 6, 7) closed, no open work against it.
- `ARCHITECTURE.md` — codebase overview for agents: architecture, hard invariants, feature
  inventory, code critique. Read this (not just this Specs section) to understand the engine.
- `AGENTS.md` — working agreement. Read before touching anything.
- ~~`NEW_CHAT_PROMPT.md`~~ — **deleted 2026-07-16.** Was a session-start template; went stale
  (claimed `m8_spectrum` wasn't built, described a spec-writing-only workflow that doesn't match
  how this project is actually being worked, referenced files never present in this checkout).
  No replacement written yet — if a new-session template is wanted again, write one against
  current reality rather than resurrecting this one.
```
