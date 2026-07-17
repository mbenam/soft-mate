# M8 Tracker Clone — Status

Last updated: 2026-07-17

A software clone of the **Dirtywave M8 Tracker**: the tracker workflow, the 2D view
navigation, the custom UI layout, and the audio engine.

**Where it stands:** songs written on real M8 hardware load, save, and play through this
engine — correct tempo, sample-accurate, note release, no DC, no glitches. The full audio
analysis + hardware capture toolchain is built, **now including unattended, framebuffer-verified
device control** (`m8_nav`, Tier 3 — the harness loads probes on the headless itself, no human
touch). **All four M8 synth engines now make their own sound:** MacroSynth is a real
**Mutable Instruments Braids** port (no longer a saw), and FMSynth, WavSynth, and HyperSynth
are implemented and audible. **Tables execute** at tick time. The remaining gap is *fidelity*,
not silence — the FM/Wav/Hyper engines are reference-*approximations* validated by offline
"is it finite / non-silent / does the parameter change the spectrum" tests, not yet
hardware-parity-verified per patch.

**Direction (decided 2026-07-17).** The synths/features were implemented from their known
**reference algorithms** (MacroSynth = open-source Mutable Instruments **Braids**, ported;
FM/wavetable/supersaw are standard, well-documented DSP, approximated), validated by
**offline math/spectral unit tests** (`m8_analyze`/`AudioMetrics`, no hardware). Hardware
audio parity with the real M8 is a later *acceptance gate*, not a development *driver*: the
parity rig is built and stays, but chasing per-unit capture config (USB capture level, output
taper) was stalling the actual feature work, and porting the reference DSP gets us closer to
the M8 than curve-fitting to a capture anyway. See **Roadmap**.

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
- **Tests**: Catch2 v3 — 128 cases
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
- `m8_composesong` — authors the startup song "SUNRISE" and writes `songs/sunrise.m8s` (data,
  not hard-coded into the app); the app loads it at startup, falling back to the in-code
  "Night Drive" demo if the file is missing. Also `m8_makesong`.
- `m8_capture` — drives the headless over serial + records USB audio → trimmed WAV;
  `--batch` (loop a probe list), `--keyjazz N` (play a live note instead of the PLAY toggle)
- `m8_nav` — **NEW.** Decodes the M8 serial *display* (SLIP framebuffer) into a text grid and
  drives the device closed-loop: `--load-file NAME` loads any probe on the headless **fully
  unattended** (browser navigation, filename verified on screen before load); `--keys` /
  `--dump-screen` / `--json` for scripted control + inspection. Serial only (no engine/SDL/audio).
- `m8_tests` — 128 cases

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
Chain transpose, **gated per-instrument by the TRANSP flag** (TRANSP OFF ignores transpose, e.g.
for drum samples — `Engine.cpp`, test B4.12). Groove/swing. FX `DEL`/`KIL`/`HOP` (clamped).
Bounds-checked, fuzzed 10k under ASan. Note release on chain end.

### Audio engine
Stereo, 8 monophonic voices, per-track vol, equal-power pan, per-instrument dry/chorus/delay/
reverb. Master chorus/delay/reverb/`tanh`, feedback clamped, DC blockers on feedback paths and
master bus. Tempo verified on real songs.

### Synth engines — all four are now audible (2026-07-17)
The M8's four synth instrument types each render their own sound; none is the old shared saw.
- **MacroSynth = Braids** (`INST_MACROSYN`, `FMSYNTH_IMPLEMENTATION.md`-adjacent work; ported
  `src/engine/braids/` + `src/engine/stmlib/`, Mutable Instruments, MIT). Shapes `0x00–0x2B`
  drive a real `braids::MacroOscillator` (24-sample block render, `m_braidsBuffer`); `shape`
  selects the model, `timbre`/`color` map to the oscillator's two parameters, pitch converts
  to Braids' 7-bit-fractional pitch scale. Bundled wavetable data is real (`braids/data/
  waves.bin` 33 KB, `map.bin`). The old polyBLEP saw is now **only a fallback** for shapes
  outside `0x00–0x2B`. Test: `[macrosynth]` renders all 44 shapes finite, non-silent, no
  clip, zero alloc.
- **HyperSynth** (`INST_HYPERSYN`) — supersaw swarm: `default_chord[]` notes × `kHyperVoices`
  detuned saws with `swarm` spread, stereo `width`, `shift` transpose, and a `subosc`.
  polyBLEP-corrected. Tests: `[hypersynth]` (chord renders clean; swarm/width change output).
- **FMSynth** (`INST_FMSYNTH`, `FMSYNTH_IMPLEMENTATION.md`) — 4 operators, 12 algorithms,
  procedural wavetable oscillators (12 base shapes), per-op ratio/level/feedback/retrigger and
  a per-op mod-slot decode. Loads/saves via `SongIO`. Tests: `[fmsynth]` (all 12 algos finite/
  non-silent; algo, shape, and feedback each change output; zero alloc). *Reference-approximation,
  not hardware-verified* — see Placeholders for the caveats it carries.
- **WavSynth** (`INST_WAVSYNTH`, `WAVSYNTH_IMPLEMENTATION.md`) — 9 base shapes generated per
  note-on into a 2048-sample buffer with SIZE / MULT / WARP / SCAN(mirror) shaping, plus WAV
  filter modes 8–11 applied into the buffer. Loads/saves. Tests: `[wavsynth]` (all 9 shapes
  finite/non-silent; shape/size/mult change output; zero alloc). *Reference-approximation;
  wavetable shapes 9+ alias to sine (no dumped wavetable data).*

All four load and save through `SongIO` (`convertSongToEngine`/`convertEngineToSong`/
`buildSongFromEngine`) and route pan/dry/sends + TRANSP gating in `Engine.cpp` exactly like the
sampler/macrosynth path.

### Tables (`TABLE_IMPLEMENTATION.md`) — executed at tick time (2026-07-17)
Per-instrument sub-sequencers now run: `Engine::tickTable()` advances an assigned table per
track, applies each row's **transpose** (semitones) and **volume** to the voice
(`SynthVoice::setTableModulation`), and executes table-internal FX **HOP** (jump row), **TIC**
(per-column tick rate), **VOL**, and **PIT**. Tables are assigned from phrase FX **TBL**;
**GRV** sets a per-track groove override (`trackGroove[8]`). Runs independent of global groove
timing. Tests: `[tables]` (6 cases). Previously stored/preserved but never executed.

### Sampler (`M8_SAMPLER_SPEC_V2.md`, hardware-verified)
Region `[LOOP ST, LOOP ST+LENGTH]` sample-relative. Play modes 00–08, reflecting overshoot.
Stereo reads, linear interp, `double` phase. Root C-4, DETUNE 1/16 semitone, SR correction.
`LOOP ST`/`LENGTH` live per-sample. DEGRADE, AMP, LIM (CLIP/SIN/FOLD/WRAP + POST/POST:AD),
FILTER (LP/HP/BP/BS + ZDF LP/HP).
**Instrument screen sealed (2026-07-17):** every SAMPLER-screen field now loads, renders (where
modeled), is editable, **and saves** — see Persistence. `DETUNE` loads/saves correctly (file
`fine_pitch` is a *signed* offset, engine detune is *unsigned* 0x80-centre: `detune = fine_pitch +
0x80`); the previous hardcode-to-centre bug is gone. `TRANSP` loads/saves and now gates transpose
in the engine.
**Note tracking (2026-07-17):** the sampler is now chromatic — a note above root C-4 plays the
sample proportionally faster/higher (`SynthVoice.cpp`, `kSamplerRootMidi = 60`; test S-NOTE1:
C-4 → 262 Hz, C-5 → 525 Hz). This was previously absent (`kSamplerRootMidi` was dead code): every
note played at one pitch, so sampler melodies didn't work. Demo drums are triggered at C-4, so they
are unaffected. *Behaviour change:* a real song that plays a sampler at non-root notes now repitches
(as hardware does) where before it did not.
**Phase 4 DSP (2026-07-17):** FILTER 06/07 (ZDF LP/HP) implemented as a topology-preserving-transform
SVF (Cytomic/Zavalishin reference, `ZdfFilter.h`; tests S-ZDF1/2/3 — pass/stop band + high-res
stability). LIM 04/05 (POST / POST:AD) implemented: for these modes the AMP gain and its clipping
apply *after* the filter (hard clip / `tanh` soft clip; test S-LIM-POST). All offline math-reference
tests, no hardware needed.
Remaining sampler *behavior* (not screen coverage): SLICE playback, REPITCH/BPM play modes 09–0E,
sample REC/EDIT, FILTER 05 (LP>HP), LIM 06–08 (POST:W1–W3) — see the notes below and
`M8_SAMPLER_COMPLETION_SPEC.md` Phases 2–4.

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
**Instrument edits now persist (2026-07-17):** `convertEngineToSong` gained the instrument
overlay loop it was missing — Sampler and MacroSynth screen fields (play/slice/start/loop/length/
degrade/transpose/table_tick + the synth-params subset + DETUNE via `fine_pitch`) are written back
on save, overlaying only modeled fields so the byte-identical round-trip still holds (tests S-RT1,
S-DET2). Previously the `engine→file` mappers were dead code and instrument edits were silently
discarded on save. (MacroSynth `shape/timbre/color` now round-trip too — groundwork for Braids.)

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
- **`m8_capture`** — Win32 serial + miniaudio capture, onset-trimmed WAV, `--start-mask`/
  `--stop-mask`. Standalone (no engine, no SDL). Proven against a real headless (firmware 6.5.2):
  pinned PLAY-toggle masks, captured a clean C-4. **`--batch` now loops a `name<TAB>label` list**
  (opens serial+audio once, prompts per probe). **`--keyjazz N`** plays a live note on the
  current instrument (`K note vel` … `K 0xFF`) instead of the PLAY toggle — a from-scratch note
  with no song.
- **`m8_nav` (Tier 3, `M8_HARDWARE_TEST_SPEC.md` §8.2b) — hardware-verified.** Decodes the M8's
  SLIP display protocol (`0xFD` draw-char → text grid, `0xFE` rect → highlight/clear, `0xFF`
  system info → hw/firmware) exactly as m8c does; auto-detects the cell pitch. `--load-file`
  is a **closed-loop** navigator (read the screen, steer, re-read — never blind key-counting,
  which the M8's ~150 ms key auto-repeat defeats): normalises the start screen (the device keeps
  whatever view it was left on — no auto-home), climbs to PROJECT, opens LOAD, accepts the
  "LOSE CHANGES?" confirm, scrolls the file list, verifies the highlighted filename, loads, and
  leaves the device on SONG. Direction masks pinned live via the framebuffer (SHIFT `0x10`,
  UP `0x40`, DOWN `0x20`, LEFT `0x80`, RIGHT `0x04`, EDIT `0x01`, OPT `0x02` = back/cancel;
  PLAY `0x08` was already pinned). Proven on the real headless: loaded `probe_sampler`/
  `probe_shape_*` unattended.
- **Two bugs found and fixed during the parity build (both real product bugs):**
  1. `m8_makeprobe`'s `buildProbeSong` serialised `song.midi_settings` but never initialised
     it (`MidiSettings` has no default initialisers), so every probe carried ~25 bytes of
     **uninitialised memory** in the MIDI-routing block. Garbage there routes the M8's tracks
     to MIDI I/O instead of internal audio ⇒ the device played **silence**. It was invisible
     because it's uninitialised memory (`probe_selftest`, written first, happened to get zeros
     and worked; the sweep loop reused dirty memory). Fixed with `song.midi_settings = {}`;
     probes are now deterministic and byte-identical to the known-good self-test probe.
  2. `SongIO.cpp convertSongToEngine` read a `Sampler`'s `sample_path` from the file but never
     copied it into the **engine** instrument's `samplePath`, so `m8_render` (which keys sample
     loading off `state.instruments[i].sampler.samplePath`) could not load **any** sampler's
     sample for a loaded song — the sampler rendered silent. Fixed (one `strncpy` in the
     sampler branch); the sampler oracle now renders correctly.
- **Sampler parity probe (`--type sampler --sample-path`, §9.1)** — `m8_makeprobe` now builds a
  sampler probe (bundled sine WAV), `verifyRoundTrip` is type-aware, and our engine's render of
  it **matches the source sine** (fundamental 263 Hz both, harmonic Δ −0.4 dB) — an offline
  proof that our sampler is faithful. This is the honest timbre gate MacroSynth can't be yet.

### Startup / demo songs
The app loads **`songs/sunrise.m8s`** at startup (authored by the `m8_composesong` tool —
"SUNRISE", 128 BPM four-on-the-floor, A-minor, sampler drum kit + MacroSynth, 16-bar build).
It is committed data, not baked into the binary. If the file is missing, the app falls back to
`loadDemoSong()` — the in-code "Night Drive" demo (16 bars, C minor, 124 BPM, swing, drums
synthesized at startup). `songs/opening.m8s` is an earlier committed song kept alongside it.

### Tests — 128 cases
Tags: `[tempo] [walk] [fx] [groove] [commands] [sample_pool] [sampler] [modulation]
[rt_safety] [demo] [io] [audio] [macrosynth] [hypersynth] [fmsynth] [wavsynth] [tables] [ui]`.
Offline against `m8_engine`, no audio device. All pass under x64 ASan (verified 2026-07-17:
128/128, ~892k assertions). Weightiest: B8.1, B3.3, B4.9 (10k fuzz), B7.2, L4, L7, M2, M12,
A3, A5. New engine tags: `[macrosynth]` (all 44 Braids shapes), `[fmsynth]` (12 algos),
`[wavsynth]` (9 shapes), `[hypersynth]`, `[tables]` (6 cases).

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

- **Synth fidelity is reference-*approximation*, not hardware parity.** MacroSynth is a real
  Braids port (see Implemented), so it is faithful; but **FMSynth and WavSynth are procedural
  approximations** whose validation is only "finite / non-silent / parameters change the
  spectrum," not an A/B against captured hardware. Known approximations carried in the code:
  FM per-op mod routing encoding is community-reverse-engineered (not hw-verified), the two
  mod slots are merged by averaging, the PIT per-op destination is a TODO, and `kFMModIndex`
  is an untuned constant (`FMSYNTH_IMPLEMENTATION.md` §10). WavSynth wavetable shapes 9+ alias
  to sine (no dumped wavetable data), and its base-shape edges are un-band-limited
  (`WAVSYNTH_IMPLEMENTATION.md` §10). MacroSynth shapes **above `0x2B`** still fall back to the
  polyBLEP saw. The parity rig (`m8_makeprobe` + `m8_capture` + `m8_spectrum`) exists to close
  these gaps at the acceptance-gate stage.
- **`PLAY` 09–0E** (REPITCH/BPM) fall back to the nearest 00–08 mode. Screen-mapped (device photos,
  2026-07-17): REPITCH modes expose a **STEPS** parameter, BPM modes a **BPM** parameter, in the row
  under PLAY (default `0x80`). Confirmed STEPS is **not** the DETUNE/`fine_pitch` byte — it's a
  separate stored byte (likely `synth_params.pitch`, unconfirmed). Still blocked on the **tempo
  formula** (STEPS→count, ratio law, REP-repitch vs BPM-timestretch), which needs an audio capture —
  guessing it would violate `AGENTS.md` §4. See memory `sampler-slice-repitch-hw`.
- **`SLICE` playback** ignored (value stored/saved only). Encoding now **hardware-verified** (device
  screen, 2026-07-17): `00`=OFF, `01`=FILE (WAV-embedded markers, needs cue-chunk parsing),
  `02`–`0x80` = 2–128 equal divisions (byte value = slice count). Equal-division playback is now
  implementable — the note→slice base (C-1 = MIDI 24, derived from C-4=60) and the START/LENGTH
  interaction want a quick audible confirm first. FILE-marker mode is a separate feature. See memory
  `sampler-slice-repitch-hw`.
- **`FILTER` 05** (LP>HP) passes through (not modeled). **`LIM` 06–08** (POST:W1–W3) fall back to
  post-filter hard clip — the "folding distortion" curves are not hardware-verified. **`LFO`
  0x0D–0x16** alias to simpler forms. **`MOD BINV`** a guess. **DRUM ENV** duck curve approximated.
  (`FILTER` 06/07 ZDF and `LIM` 04/05 POST/POST:AD are now implemented — see Sampler above.)
- **`MOD RATE` / rate half of `MOD BOTH`/`MOD BINV` do nothing** — only the amount half of
  mod-to-mod routing is applied. Was previously a dead `rateScale` array (computed, never
  read); removed rather than left looking implemented (`CODE_CLEANUP_SPEC.md` #8).
- **Voice path is mono** (`SamplerEngine` reads stereo, `SynthVoice` sums).

---

## Not implemented

- **MIDIOut, ExternalInst** — preserved on save, silent on play. (WavSynth, FMSynth,
  HyperSynth are now **implemented** — see Synth engines above.)
- **Scales**, **SLICE** — stored/preserved, not read by the engine. (Tables are now
  **executed** — see Tables above.)
- **FX `VOL`/`PIT`/`REV` in phrase steps** — parsed, inert at phrase level. (`VOL`/`PIT`
  *are* executed inside tables; `REV` is still a stub everywhere. `TBL`/`GRV`/`TIC` are now
  live — see `FX_COMMANDS_SPEC.md` for the full per-command matrix and the long list of
  M8 FX commands still absent, e.g. ARP/RET/RND/RETRIG/scale+arp commands.)
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
Root C-4 (MIDI 60)   DETUNE 1/16 semitone/step, 0x80 centre (file fine_pitch is signed offset, engine detune is unsigned: detune = fine_pitch + 0x80)
Env times IN TICKS, tempo-relative
LOOP window [LOOP ST, LOOP ST+LENGTH], relative to the WHOLE SAMPLE  (only inference; test S6)
```

### Serial control protocol (`m8_capture`) — pinned 2026-07-16, firmware 6+

Empirically pinned per `M8_HARDWARE_TEST_SPEC.md` §5 against a real headless on COM3, using
the captured audio as the oracle (not a human ear). Recorded in `hw_buttons.json`.

```
Controller byte: 'C' <keymask>, then 'C' 0x00 to release.
PLAY   0x08   Start playback from cursor. It is a TOGGLE — pressing PLAY again STOPS.
             => start_mask == stop_mask == 0x08. There is NO separate stop key.
```

The old defaults (`start 0x08` guess was right; `stop 0x10 = KEY_A` was wrong) are fixed in
`main_capture.cpp`. `m8_hwtest.ps1` confirms STOP by re-pressing the toggle and verifying
silence, not by sweeping for a distinct key (a sweep is actively wrong for a toggle).

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
- **`m8_capture` — PROVEN against the device (firmware 6.5.2, COM3).** Onset trim verified on
  real captured WAVs; PLAY-toggle masks pinned. See the serial-control-protocol block under
  Hardware-verified constants.
- **OPEN: USB capture level ~100× too low after a device power-cycle.** On 2026-07-17 every
  capture came back ~100× quieter than the earlier session — even a sampler playing a bundled
  **full-scale sine** (sample confirmed loaded on-device) captured at peak ≈ 0.006–0.04.
  Raising the M8 `OUTPUT VOL` (mixer) did **not** change the capture, because that control feeds
  the headphone/line out, not the USB audio tap. Leading hypothesis: the **Windows recording
  level** for "Digital Audio Interface (M8)" was reset by USB re-enumeration on power-cycle
  (Tier 1's loud captures pre-dated the reboot). Not yet confirmed/fixed — and now lower
  priority, since audio parity is an acceptance gate, not a blocker for feature work. A second,
  independent issue: the probe's amp is an AHD→VOLUME mod that **decays to zero** and the single
  sequenced note doesn't retrigger fast enough, so captures are a ~0.5 s blip, not a sustained
  tone. When parity work resumes, give parity probes a **sustaining** amp (drop the decaying
  mod) so there's a steady tone to spectrum-analyse.
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

**Strategy (2026-07-17):** build each feature from its **reference algorithm** and validate with
**offline** math/spectral unit tests (Catch2 + `AudioMetrics`); treat M8 hardware capture as a
later acceptance gate, not a per-feature step. The parity rig (`m8_makeprobe` → `m8_nav
--load-file` → `m8_capture` → `m8_render` → `m8_spectrum`) is built and waits for that gate.

1. **MacroSynth → Braids — DONE (2026-07-17).** Ported Mutable Instruments **Braids** (MIT):
   shapes `0x00–0x2B` render through `braids::MacroOscillator`; `shape`/`timbre`/`color` drive
   the model. `[macrosynth]` test covers all 44 shapes. *Remaining:* per-model spectral parity
   against captured hardware (acceptance gate, item 6), and `redux`/shapes above `0x2B`.
2. **Tables — DONE (2026-07-17).** `Engine::tickTable()` executes assigned tables (transpose/
   volume + HOP/TIC/VOL/PIT), TBL/GRV/TIC phrase FX wired. `[tables]` tests. See
   `TABLE_IMPLEMENTATION.md`.
3. **WavSynth / FMSynth / HyperSynth — DONE (2026-07-17), approximation-grade.** All three make
   sound and load/save (`[wavsynth]`/`[fmsynth]`/`[hypersynth]` tests). *Remaining:* fidelity —
   FM mod-routing/index tuning, WavSynth wavetable data (shapes 9+ alias to sine), and hardware
   A/B (item 6). See `FMSYNTH_IMPLEMENTATION.md` / `WAVSYNTH_IMPLEMENTATION.md` §10 caveats.
4. **SLICE** (equal-division encoding now hardware-verified — implementable; FILE-marker mode and
   the note-base/START-interaction confirm remain), then the REPITCH / BPM play modes, which are
   still **blocked on a device capture** for the STEPS/BPM byte + tempo formula (see Placeholders);
   do not guess the formula.
5. **Scales** (note→frequency), **stereo voice path**, FILTER 05 (LP>HP), the aliased LIM 06–08 /
   LFO modes, FX `VOL`/`PIT`/`REV`, project EQ/limiter/DJF — quality/coverage cleanups. (ZDF
   filters and LIM POST/POST:AD are **done** — 2026-07-17.)
6. **Hardware audio-parity acceptance pass** (once the above land): resolve the USB-capture-level
   config (Known issues), use sustaining parity probes, and confirm capture-vs-render distance
   shrinks toward zero — first for the sampler (already at parity), then per Braids model.

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
- `M8_CAPTURE_SPEC.md` — implemented; **proven against the device 2026-07-16** (firmware 6+,
  COM3). Button masks pinned (PLAY toggle = 0x08), a clean C-4 macrosynth probe captured.
- `M8_HARDWARE_TEST_SPEC.md` — **Tier 1 PASSING; Tier 2 + Tier 3 enablers now built
  (2026-07-17).** `m8_hwtest.ps1` chains makeprobe → render oracle → capture → analyze →
  spectrum into one `verdict.json`; T0 (makeprobe round-trip) runs in CI (`test_persistence.cpp`,
  `[io]`). **Tier 2:** `m8_capture --batch` implemented. **Tier 3 (§8.2b):** `m8_nav` decodes
  the SLIP framebuffer and loads probes on the headless **fully unattended** (see the tooling
  section) — the last human step is gone. **§9.1 sampler probe:** implemented and validated
  offline (our render matches the bundled sine). The one remaining open item is device-side, not
  tooling: USB captures are ~100× too quiet since a power-cycle (Known issues) — deferred,
  because audio parity is now an acceptance gate, not an active driver (see Roadmap). Earlier
  fixes stand: the spectrum fundamental estimator uses autocorrelation (a bright Braids timbre
  defeats a spectral global-max).
- `FMSYNTH_IMPLEMENTATION.md` / `WAVSYNTH_IMPLEMENTATION.md` / `TABLE_IMPLEMENTATION.md` —
  **new (2026-07-17), implemented.** Implementation plans for the FMSynth, WavSynth, and Table
  work; the code matches them and the `[fmsynth]`/`[wavsynth]`/`[tables]` tests pass. Each
  carries a §10 "Known limitations" list (the approximation caveats summarised under Placeholders).
  MacroSynth/Braids has no standalone spec — the port lives in `src/engine/braids/` + `stmlib/`.
- `FX_COMMANDS_SPEC.md` — **new (2026-07-17).** Full per-command matrix (what works in the phrase
  engine vs table engine vs file I/O vs UI), the relative/absolute value contract, and the long
  list of M8 FX commands still absent. TBL/GRV/TIC implemented; VOL/PIT execute in tables; REV
  still a stub; phrase-level VOL/PIT still inert.
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
