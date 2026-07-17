# m8-sdl3 — Codebase Overview, Invariants, and Critique

> **Audience:** AI agents and new contributors. This document was produced from a
> full read of the source tree (`src/`, `tests/`, `CMakeLists.txt`, `tests/ui/*.m8script`)
> — *not* from the other markdown specs. It describes what actually exists in the
> code, the load-bearing design rules you must not break, and known weaknesses.
>
> **Read the "Hard Invariants" section before changing anything in `src/engine/`.**

---

## 1. What this project is

A software clone of the **Dirtywave M8 tracker** written in C++20, built with CMake.
It reproduces the M8's 320×240 tracker UI (SDL3), its Song → Chain → Phrase
sequencing model, a per-track voice engine (sampler + **all four M8 synth
engines**: MacroSynth via a ported Mutable Instruments **Braids**, plus
HyperSynth / FMSynth / WavSynth) with M8-style modulators and per-instrument
**Tables** (sub-sequencers), a send-effects bus (chorus / delay / reverb via
DaisySP), and **real `.m8s` song-file persistence** through the `m8-files-cxx`
submodule.

The repo is structured so that the *entire audio engine is testable without SDL
or an audio device*, and the *entire UI is testable without a human* via a
scripting harness.

### Build targets (CMakeLists.txt)

| Target | Sources | Links | Purpose |
|---|---|---|---|
| `m8_engine` (static lib) | `src/engine/*`, `src/io/SongIO.cpp`, `src/analysis/AudioMetrics.cpp` | DaisySP, `m8_files_cpp`, kissfft | The whole audio core. **No SDL.** |
| `m8_clone` (gated by `BUILD_CLONE`) | `src/main.cpp`, `src/ui/**` | SDL3 + `m8_engine` | The interactive app. |
| `m8_render` | `src/tools/main_render.cpp` | `m8_engine` | Offline WAV renderer + event-CSV logger. Headless ground truth. |
| `m8_analyze` | `src/tools/main_analyze.cpp` | `m8_engine` | WAV metrics checker (peak/DC/crest/clip/silence), `--diff` mode. |
| `m8_makeprobe` | `src/tools/main_makeprobe.cpp` | `m8_files_cpp` only | Generates minimal `.m8s` probe songs (one instrument, one note). |
| `m8_composesong` / `m8_makesong` | `src/tools/main_composesong.cpp` / `main_makesong.cpp` | `m8_engine` | Author songs to `.m8s` as data. `m8_composesong` writes `songs/sunrise.m8s` (the startup song). |
| `m8_capture` | `src/tools/main_capture.cpp` | miniaudio (header-only) | Records real M8 hardware over serial + USB audio, for A/B reference. `--batch`, `--keyjazz`. |
| `m8_nav` | `src/tools/main_nav.cpp` | none (Win32 serial only) | Decodes the M8 SLIP **display** stream into a text grid and drives the headless closed-loop; `--load-file` loads a probe fully unattended (Tier 3). |
| `m8_tests` | `tests/*` | Catch2 v3 + `m8_engine` | 128 test cases across ~20 files (verified 2026-07-17). |

Third-party: `third_party/m8-files-cxx` (git submodule — `.m8s` read/write),
`third_party/kissfft` (FFT for analysis), `third_party/miniaudio` (capture tool
only), plus FetchContent for SDL3 (`preview-3.1.3`), DaisySP, Catch2.

`src/main_stage1.cpp` is an **older, superseded prototype** of the UI kept in the
tree but not built into any target. Ignore it; don't extend it.

---

## 2. Core architecture: two threads, three rings

This is the most important thing to understand. Everything else hangs off it.

```
 UI / main thread                          Audio thread (SDL callback)
┌─────────────────────────┐               ┌──────────────────────────────┐
│ uiSequencer (mirror)    │               │ Engine::render(buf, frames)  │
│ uiEngineState (mirror)  │  EngineCommand│   per frame:                 │
│                         │  ───────────► │   processCommands()          │
│ edits mutate the mirror │  CommandRing  │   tick sequencer, run voices │
│ AND push a command      │  <1024, SPSC> │   mix + chorus/delay/reverb  │
│                         │               │                              │
│ drains every frame:     │ ◄───────────  │ emits EngineEvent (NOTE_ON…) │
│  - EngineEvent ring     │  EventRing    │                              │
│  - gcRing (frees WAVs)  │ ◄───────────  │ pushes freed SampleData      │
│  - songGcRing (delete[])│ ◄───────────  │ pushes old LOAD_SONG buffer  │
│                         │               │                              │
│ reads playheads via     │ ◄───────────  │ publishPlayhead(): packed    │
│  atomic<uint32_t>[8]    │   atomics     │  u32 per track, rel/acq      │
└─────────────────────────┘               └──────────────────────────────┘
```

- **`CommandRing<T, N>`** (`src/engine/CommandRing.h`) is a lock-free
  **single-producer / single-consumer** ring (power-of-two capacity,
  acquire/release atomics). `EngineCommand` is a trivially-copyable POD with a
  union payload (`Step` / `ChainStep` / `SampleData` / `SongPayload`) —
  enforced by `static_assert(std::is_trivially_copyable_v<...>)`.
- **State ownership:** the audio thread owns the authoritative
  `Sequencer` + `EngineState`. The UI keeps *mirror copies* (`uiSequencer`,
  `uiEngineState` in `main.cpp`) that it mutates locally for instant display
  and then reconciles by sending the same mutation as a command
  (`SET_STEP`, `UPDATE_PARAM`, …). `EngineStateUpdater::applyParameterUpdate`
  is the **shared** param-application function used by *both* sides so the two
  copies stay in sync.
- **Memory across the boundary:**
  - Samples: the UI decodes WAVs (dr_wav via `FileBrowser::loadWavFile`) and
    passes ownership through `LOAD_SAMPLE`. The audio thread only does pointer
    swaps and refcounts (`SamplePool`, 128 slots). Buffers to be freed travel
    *back* on `m_gcRing`; the UI frees them. Ring overflow deliberately leaks
    (documented in `Engine.cpp`) — leaking beats blocking or freeing on the
    audio thread.
  - Songs: `LOAD_SONG` carries **one** heap allocation laid out as
    `[Sequencer][EngineState]`; the audio thread memcpys it in and returns the
    pointer on `m_songGcRing` for the UI to `delete[]`.
- **Playhead feedback** is not via the event ring but via 8 packed
  `std::atomic<uint32_t>` (song row ≪16 | chain row ≪8 | phrase row, plus
  playMode in bits 24–25 and activeCol in bits 26–28), published with
  `memory_order_release`, read with acquire. `Engine::getPlayhead()` unpacks it.

### The sequencer model (`Sequencer.h`, `SeqTypes.h`, `Engine.cpp`)

- 256 phrases × 16 `Step` rows (note / vol / instrument / 3 FX slots),
  256 chains × 16 (`phrase`, signed transpose), 256 song rows × 8 tracks,
  32 grooves (ticks-per-row patterns), 256 tables (allocated, **not yet played**).
- Timing: `BPM * 4 rows/beat`, default 6 ticks per row; a **groove** replaces
  the 6 with a per-row tick count (e.g. 7/5 swing). Tick length in samples is
  recomputed only on BPM change (`recalcBPM`), and modulator envelope times are
  expressed in **ticks** so they follow tempo (`EnvContext::samplesPerTick`).
- Play modes: `PHRASE` / `CHAIN` (single active track, others force note-off)
  and `SONG` (8 tracks walk chains independently; when any track's chain ends,
  `m_songRowAdvance` moves **all** tracks to the next song row together —
  `doTick()` → `syncSongRow()`).
- Step FX implemented: **DEL** (delay trigger by N ticks), **KIL** (kill note at
  tick N), **HOP** (jump to phrase row). `VOL`/`PIT`/`REV` exist in the enum and
  file mapping but are **not executed** by `tickTrack`.

### The voice (`SynthVoice`, `SamplerEngine`, `Envelopes.h`, `Lfo.h`, `Modulation.h`)

- 8 voices, strictly **one per track, monophonic**.
- Instrument types (`InstType`): `INST_SAMPLER`, `INST_MACROSYN`, `INST_HYPERSYN`,
  `INST_FMSYNTH`, `INST_WAVSYNTH`, `INST_MIDI`, `INST_NONE`. The render paths live in
  `SynthVoice::renderSample` (booleans `isBraids`/`isHyper`/`isFM`/`isWav`; the polyBLEP saw
  is now only the fallback for none-of-the-above):
  - `INST_SAMPLER` — full sample playback: FWD/REV/loop/ping-pong/osc modes, region from
    start/loop_st/length bytes, linear interpolation, detune at 1/16-semitone steps,
    sample-rate ratio correction.
  - `INST_MACROSYN` — **a real Mutable Instruments Braids port** (`src/engine/braids/` +
    `src/engine/stmlib/`, MIT). Shapes `0x00–0x2B` drive `braids::MacroOscillator` (rendered in
    24-sample blocks into `m_braidsBuffer`); `shape` picks the model, `timbre`/`color` map to
    its two parameters. Shapes above `0x2B` fall back to the polyBLEP saw. Bundled wave data:
    `braids/data/waves.bin`, `map.bin`.
  - `INST_HYPERSYN` — supersaw: `default_chord[]` notes × detuned polyBLEP saws with `swarm`
    spread, stereo `width`, `shift` transpose, and a `subosc`.
  - `INST_FMSYNTH` — 4-operator / 12-algorithm FM with procedural wavetable oscillators
    (`initFMWavetables`), per-op ratio/level/feedback/retrigger + mod-slot decode. *Reference
    approximation, not hardware-verified* (see `FMSYNTH_IMPLEMENTATION.md` §10).
  - `INST_WAVSYNTH` — 9 base shapes generated per note-on (`generateWavShape`) with
    SIZE/MULT/WARP/SCAN shaping and WAV filter modes 8–11. *Approximation; wavetable shapes 9+
    alias to sine* (`WAVSYNTH_IMPLEMENTATION.md` §10).
  - `INST_MIDI`/`INST_NONE` render nothing.
- **Tables** (per-instrument sub-sequencers) execute at tick time: `Engine::tickTable()` applies
  each row's transpose/volume to the voice (`SynthVoice::setTableModulation`) and runs table FX
  (HOP/TIC/VOL/PIT); assigned via phrase FX `TBL`, with `GRV` setting a per-track groove override
  (`trackGroove[8]`). See `TABLE_IMPLEMENTATION.md` / `FX_COMMANDS_SPEC.md`.
- Per-voice DSP chain: sample/osc → degrade (sample-and-hold decimator) →
  amp drive (up to 8×) → limiter modes (CLIP / SIN fold / FOLD / WRAP) →
  SVF filter (LP/HP/BP) → gate ramp (3 ms anti-click) × volume.
- 4 modulator slots per instrument, M8-style: AHD, ADSR, DRUM env, LFO
  (23 shape codes incl. envelope-shaped one-shots), TRIG env (retriggered when a
  chosen *source instrument* plays anywhere — `Engine::notifyTrigSource`), and
  TRACKING. Destinations: VOLUME, PITCH (±1 octave), LOOP_ST, LENGTH, DEGRADE,
  CUTOFF (±5 octaves), RES, AMP, PAN(stored, unused), and mod-to-mod
  (`MOD_AMT`/`MOD_RATE`/`MOD_BOTH`/`MOD_BINV` scale the *next* slot).
  Amounts are bipolar around 0x80 (`bipolarAmt`).
- Master bus: per-track volume → constant-power pan → dry + three sends →
  DaisySP Chorus / dual 2-s DelayLine with feedback + DC-blockers / ReverbSC →
  master volume → DC-block → `tanh` soft clip → hard clamp.

### Persistence (`src/io/SongIO.*`)

- `loadSong(path, sampleRoot)` reads a real `.m8s` via `m8-files-cxx`, converts
  library structs → engine structs (FX command remap, `std::visit` over
  instrument/modulator variants), collects + deduplicates sample paths,
  resolves them against `sampleRoot` (M8 paths are absolute `/Samples/...`;
  leading `/` is stripped), and reports `missing` paths. Keeps the **original
  file bytes** in `LoadResult::original`.
- `saveSong` **re-parses the original bytes and overlays engine state onto that
  song object**, then `write_over`s the original buffer. This is the mechanism
  that preserves unimplemented data (MIDI out, EQs, unmodelled fields...)
  byte-for-byte. Pre-4.0 files load read-only (`writable == false`); save
  refuses them.
- Sampler, MacroSynth, HyperSynth, FMSynth, and WavSynth instruments all now
  **load into their engine types and save back** (`convertSongToEngine` /
  `convertEngineToSong` / `buildSongFromEngine` each have a branch per type);
  modeled fields are overlaid so the byte-identical round-trip still holds for
  untouched data (test L7). `INST_MIDI`/external types still load as `INST_NONE`
  (silent) but round-trip intact.

### UI (`src/main.cpp`, `src/ui/**`)

- `Renderer` wraps SDL3 with an 8×8-cell character grid (custom `font.h`) and —
  crucially — mirrors every draw into a **40×30 "VRAM" shadow grid**
  (`VirtualCell`: char, color, bg, bracket flag, slider fill, per-frame write
  count). This is what makes headless UI assertions possible: text dumps, JSON
  dumps, overlap detection.
- `ViewManager` is an M8-style **Shift+Arrow map of screens** on an (x, y)
  grid: x = Song/Chain/Phrase/Instrument/Table, y layers for
  Project/Groove/Scale/Mods/Pool/Mixer/Effects, plus a modal stack used by the
  file browser.
- Each screen lives in `src/ui/screens/<name>/` as a `Render<Name>Screen(...)`
  free function plus a `<Name>ScreenLayout.h` containing static cell layouts
  and (for form-like screens) a **`NavNode<CursorId>` navigation graph**
  (`CursorId::CUTOFF → {up,down,left,right}`) that drives cursor movement.
  `CursorId` is a per-screen `enum class` (see `CODE_CLEANUP_SPEC.md` #4);
  the other 6 screens (grid-based: Song/Chain/Phrase/Groove/Table/InstPool)
  use plain `int` row/col cursors instead and have no NavNode map.
- Editing model mimics the M8: hold **X** (edit) + arrows to change values,
  tap X to insert defaults / toggle; **Shift**+arrows to switch screens;
  **Space** to play/stop (mode depends on the current screen). Every edit
  mutates the UI mirror and pushes a command; on ring overflow, edits are
  queued in `pendingEdits` and retried next frame (**resync guarantee**,
  covered by test B6.2).
- `ScriptRunner` (`--script file.m8script [--headless] [--out-dir d]`)
  executes a line-based DSL: `key`, `hold`, `type`, `wait`, `play/stop`,
  `load/save/set_sample_root`, `render_offline`, `dump_screen/dump_json/screenshot`,
  and a family of `assert_*` commands that inspect the VRAM shadow grid,
  play state, error overlays, and song name. Exit codes: 0 pass, 2 parse error,
  nonzero on assertion failure. Scripts in `tests/ui/*.m8script`; run through
  Catch2 by `tests/test_ui_scripts.cpp` shelling out to `m8_clone.exe`.

### Verification tooling (`src/analysis/`, `src/tools/`)

An unusually strong closed loop for an audio project:

1. `m8_makeprobe` writes a minimal `.m8s` with one instrument at known params.
2. `m8_capture` plays that probe on **real M8 hardware** (serial keyjazz +
   USB-audio record via miniaudio) → reference WAV.
3. `m8_render` renders the same probe with this engine → candidate WAV
   (plus a CSV of every NOTE_ON/OFF/TICK with absolute sample time).
4. `m8_analyze` / `AudioMetrics` (peak, RMS, crest, per-window DC, clipped
   count, non-finite count, longest silence, mid/side, correlation, FFT pitch,
   spectral centroid) provide objective pass/fail, and `--diff` compares WAVs.

`m8_nav` closes the last manual gap (`M8_HARDWARE_TEST_SPEC.md` Tier 3): it decodes the
M8's SLIP **display** protocol (the same one m8c speaks — `0xFD` draw-char, `0xFE` rect,
`0xFF` system-info) into a text grid, so the harness can *read the screen* and drive the
file browser closed-loop (`--load-file`), loading any probe on the headless with no human
touch. This is a diagnostic/harness tool only — **not** part of the shipped product, and it
links nothing but Win32 serial.

**Two product bugs were found and fixed via this loop (2026-07-17):** `m8_makeprobe` wrote
`song.midi_settings` without initialising it (uninitialised-memory MIDI routing ⇒ silent
probes on hardware), and `convertSongToEngine` never copied a `Sampler`'s `sample_path` into
the engine instrument (so `m8_render` could not load any sampler's sample for a loaded song).

**Note on audio parity (2026-07-17):** hardware capture-vs-render parity is now treated as a
later *acceptance gate*, not a per-feature development driver. Synths are implemented from
their reference DSP (MacroSynth = open-source Braids) and validated by **offline** spectral
unit tests. See `status.md` Roadmap.

### Test suite (tests/)

`OfflineHost` drives a real `Engine` with no audio device, collecting every
event and every audio sample, asserting **no NaN / no Inf / |s| ≤ 1** on every
chunk. `AllocGuard.cpp` globally replaces `operator new/delete` to count
allocations while `g_inAudioThread` is set — that is how "no allocation on the
audio thread" is *enforced*, not just intended. Coverage includes: tempo math,
sequencer walks (song/chain/phrase, transpose, empty-chain sync), FX
(DEL/KIL/HOP + clamping), grooves, command/playhead semantics incl. ring
overflow, sample-pool refcounting/reload-while-playing/GC-exactly-once,
sampler play modes, all modulator types (incl. a fuzz test M18), TSan smoke
test (concurrent render + command spam), demo-song audio metrics, `.m8s`
byte-identical round-trips, the UI script regression, and the synth/table
engines: `[macrosynth]` (all 44 Braids shapes finite/non-silent/no-clip),
`[hypersynth]`, `[fmsynth]` (12 algos), `[wavsynth]` (9 shapes), and
`[tables]` — each asserting zero allocation on the audio thread.

---

## 3. HARD INVARIANTS — do not violate these

These are the rules the whole design depends on. Violating any of them
introduces data races, audio glitches, or file corruption that the tests are
specifically built to catch.

1. **The audio thread never allocates, frees, blocks, or does file I/O.**
   `Engine::render` and everything it calls (`processCommands`, `tickTrack`,
   voices, `SamplePool`) must stay allocation-free. `SamplePool` does pointer
   swaps and refcounts only. Anything that needs freeing is pushed onto
   `m_gcRing` / `m_songGcRing` for the UI thread to free. The AllocGuard tests
   will fail if you break this.

2. **Each `CommandRing` is SPSC — exactly one producer and one consumer, ever.**
   The UI thread produces `EngineCommand`s; the audio thread consumes them.
   Never call `engine.render()` from the main thread while an SDL audio stream
   exists (script mode only renders manually when `stream == nullptr` — the
   comment at `main.cpp:1127` explains why). Never drain the event/GC rings
   from more than one thread.

3. **Cross-ring payloads must be trivially copyable PODs.** No
   `std::string`, no vectors, no owning smart pointers inside `EngineCommand`,
   `EngineEvent`, or `SampleData`. The `static_assert`s exist to stop you.

4. **UI mirror and engine state must receive identical mutations.** Any new
   editable parameter goes through a `ParamID` + `EngineStateUpdater` case so
   that `pushParam` applies it to `uiEngineState` *and* the engine applies the
   same code path to its own state. Do not mutate engine state directly from
   the UI, and do not add a param that only one side applies.

5. **Command sends must handle ring overflow.** Use the `commandSink` /
   `pendingEdits` retry pattern (or free the payload if the push fails, as the
   `LOAD_SAMPLE` path does). Silently dropping an edit desyncs the mirrors.

6. **Sequencer/engine geometry is fixed and index 0xFF is the empty sentinel.**
   256 phrases/chains/tables/song-rows × 16 rows, 32 grooves, 128 instruments,
   16 scales, 8 tracks, 4 mod slots. `NOTE_EMPTY == VOL_EMPTY == INST_EMPTY ==
   CHAIN_EMPTY == PHRASE_EMPTY == 0xFF`. Valid IDs are 0..254. The engine
   bounds-checks incoming command indices — keep doing that for new commands.

7. **`EngineState::instruments` is sized (128) once in the constructor and
   never resized.** Voices hold raw `const Instrument*` into it
   (`pendingInst`); a reallocation would leave dangling pointers on the audio
   thread.

8. **Saving must preserve what the engine doesn't model.** `saveSong` works by
   overlaying onto a re-parse of the original file bytes (`LoadResult::original`).
   Never rewrite save as "serialize engine state from scratch" — that would
   destroy FM/Hyper/Wav instruments, EQs, and every field this clone doesn't
   implement. Pre-4.0 files stay read-only.

9. **Envelope/LFO times are in ticks, not seconds.** Anything time-based added
   to the modulation system must scale with `EnvContext::samplesPerTick` so it
   follows tempo, matching hardware behavior.

10. **Sample loading order on the audio thread:** dedupe by path (`find` →
    `addRef`), else `install`; before releasing an old handle, note-off and
    null the sample on any voice still pointing at it; push the released
    buffer to the GC ring. Reuse this exact sequence for any new sample paths.

11. **The offline renderer and the app must produce identical audio for the
    same song.** That is why `LOAD_SONG` resets all effects DSP state and
    smoothers (test "L9 LOAD_SONG resets effects buffers"). If you add any
    stateful DSP, reset it in the `LOAD_SONG` handler too.

12. **Headless/script mode is a first-class citizen.** New screens must draw
    through `Renderer` (so the VRAM shadow grid captures them) and should get
    a `.m8script` assertion. Don't draw via raw SDL calls that bypass the
    grid; don't add UI behavior that can't run with `--headless`.

13. **Playhead publication stays wait-free.** UI reads position only via the
    packed atomics (`getPlayhead`). Don't add locks, and don't have the UI
    read engine-owned structs while audio runs (the `getStateForInit` /
    `getSequencerForInit` accessors are legal *only before the stream starts*
    or in offline tests — the name is the contract).

---

## 4. What has been achieved (feature inventory)

**Working end-to-end:**
- Real-time audio via SDL3 audio-stream callback at 48 kHz stereo, and a
  bit-equivalent offline render path (`m8_render`, script `render_offline`).
- Full Song/Chain/Phrase playback with per-track groove timing, chain
  transpose, synchronized song-row advance, empty-song guard, and
  PHRASE/CHAIN/SONG play modes started from the cursor position.
- Sampler instrument: 15 play-mode codes (fwd/rev/loops/ping-pong/osc region
  modes), region computation from start/loop/length bytes, detune,
  sample-rate conversion, degrade, drive + 4 limiter curves, SVF filter.
- Generated in-memory demo kit + "NIGHTDRIVE" demo song (kick/snare/hat/clap
  DSP-synthesized at startup — no shipped WAVs).
- The complete M8 modulation matrix shape: 4 slots × 6 modulator types ×
  14 destinations including mod-to-mod routing and cross-instrument TRIG.
- Send-effects bus (chorus, stereo delay with smoothed times + feedback
  clamp + DC blocking, ReverbSC), constant-power pan, master saturation.
- `.m8s` load/save with byte-identical round-trip for untouched data,
  missing-sample reporting, sample-root resolution, version gating.
- 12 UI screens (Song, Chain, Phrase, Instrument ×2 layouts, Table, Project,
  Groove, Scale, Mods, Instrument Pool, Mixer, Effects) + file browser modal
  + save-as / sample-root text input overlays + missing-samples overlay,
  playhead highlighting, M8-style key model.
- UI scripting harness with 16 command types and screen-content assertions;
  crash regressions pinned by scripts (`groovetest`, `pre40_refuses`, ...).
- All four M8 synth engines audible: MacroSynth (ported Braids), HyperSynth (supersaw),
  FMSynth (4-op/12-algo), WavSynth (9 shapes + SIZE/MULT/WARP/SCAN) — each with load/save
  and offline "finite / non-silent / params change the spectrum" tests.
- Tables execute at tick time (per-row transpose/volume + HOP/TIC/VOL/PIT FX), assigned via
  phrase `TBL`; `GRV` per-track groove override.
- Hardware ground-truth pipeline (probe generator → **unattended framebuffer-verified
  load on the headless** (`m8_nav`) → serial+USB capture → analyzer with hard numeric
  gates → spectral A/B). Sampler parity validated offline; MacroSynth is a faithful Braids
  port; FM/Wav/Hyper are reference-approximations awaiting the hardware A/B acceptance gate.
- RT-safety enforcement in tests: allocation counting, TSan smoke test,
  NaN/Inf/clip gates on every rendered chunk, ring-overflow resync tests.

**Stored/editable but NOT yet audible (known stubs — don't mistake for bugs):**
- MacroSynth `redux` and shapes above `0x2B` (the saw fallback). FM/Wav/Hyper *fidelity*
  gaps: FM mod-routing encoding + mod index are un-tuned guesses, WavSynth wavetable shapes
  9+ alias to sine. These make sound but aren't hardware-parity-verified.
- Scales (16 scales editable; note→frequency ignores them).
- FX commands VOL, PIT in *phrase* steps (parsed, not executed — they *do* run inside Tables);
  REV is a stub everywhere. Many M8 FX commands (ARP/RET/RND/scale/arp/mixer sends...) absent —
  see `FX_COMMANDS_SPEC.md`.
- Mixer: input/USB channels, DJ filter, limiter value, `mix_vol`.
- Effects: chorus width/reverb-send, delay width/reverb-send, reverb size /
  mod depth/freq / width (reverb LP is hardcoded 10 kHz; only decay is live).
- Instrument params: `tbl_tic`, `eq`, `slice`, sampler `transp` flag, `pan`
  mod destination.
- `INST_MIDI` type.

---

## 5. Critique

### 5.1 Architectural strengths (keep these)

- The **SPSC ring + mirror-state + shared updater** pattern is a textbook
  RT-audio architecture and it is *actually enforced by tests*, which is rare.
- Engine/UI separation is genuinely clean at the library level: `m8_engine`
  has zero SDL dependency, which is what makes the offline renderer, analyzer
  and the entire test suite possible.
- The verification story (probe → hardware capture → render → metric diff)
  and the VRAM-shadow UI assertion harness are exceptional. Most clones eyeball
  audio; this one measures it.
- Save-by-overlay is the right call for a partial implementation of a
  proprietary format.

### 5.2 Significant problems

1. **[FIXED — CODE_CLEANUP_SPEC #1]** ~~`main.cpp` is a 1,386-line god
   function.~~ All input handling for 12 screens, persistence UI, overlays,
   script glue and an audio callback live in one `main()`. The per-screen
   `if (view == X)` chains with hold-to-edit logic are near-duplicates 12
   times over. Screens render via free functions but *input* never got the
   same treatment — a `Screen::handleInput` interface is the obvious missing
   abstraction. Each screen now has a `HandleXInput`/`HandleXEditRelease`
   pair beside its `RenderXScreen`, sharing `m8::ui::CommandSink`/`PushParam`
   (`src/ui/UiCommands.h`) and the moved `AdjustU8`/`AdjustS8`/`ModifyValue`/
   `InsertDefault` helpers (`src/ui/UiEditHelpers.h`). `main.cpp` is now 817
   lines. Note: the stringly-typed NavNode-map cursor mechanism itself
   (procedurally-generated keys like `NOTE_EN_0`, `TRK_VOL_0`, `MOD_TYPE_0`)
   was intentionally left unchanged by this fix — see item 4 below, which is
   still open.

2. **Triplicated song-load block.** The ~35-line "stop → pack buffer →
   LOAD_SONG → update mirrors → missing-samples message" sequence appears
   three times in `main.cpp` (two file-browser branches at lines ~438 and ~518
   — one of which looks unreachable, since the first branch `continue`s for the
   FILE_BROWSER view — plus the key-up branch at ~1063) and a fourth time
   inside the ScriptRunner `loadSong` lambda. One helper function would remove
   ~120 lines and a whole class of divergence bugs.
   **[FIXED — CODE_CLEANUP_SPEC #2: extracted `loadSongIntoEngine`; the
   suspected-unreachable branch was confirmed dead (traced through
   `ViewManager::handleNavigation`) and removed outright. Unifying the three
   reachable copies surfaced a real bug: two of them cleared
   `LoadResult::original` after loading, which breaks any subsequent
   `saveSong()` (it needs `.original` to re-parse) — fixed by standardizing on
   the non-clearing behavior, the one path that was actually under test]**

3. **Latent out-of-bounds UB in `EngineStateUpdater`.** The first lines form
   `state.instruments[cmd.targetId]`, `inst.mods[cmd.row]`, and
   `state.scales[cmd.targetId]` **before** the switch, for *every* param —
   including mixer/effects params where `targetId`/`row` are unvalidated and
   meaningless. `scales` has 16 entries and `mods` has 4; an instrument-param
   command with `targetId = 100` forms `scales[100]` — already UB even if never
   written. `test_rt_safety.cpp` actually sends `UPDATE_PARAM` commands with
   garbage `targetId`. The references should be formed lazily inside the cases,
   with bounds checks like the other command handlers have.
   **[FIXED — CODE_CLEANUP_SPEC #3]**

4. **[FIXED — CODE_CLEANUP_SPEC #4]** ~~Stringly-typed UI navigation.~~ Cursor
   positions were `std::string` IDs (`"TRK_VOL_0"`, `active_cursor_mod.back()
   - '0'`), and NavNode maps were rebuilt (by value) on every keypress. It
   worked, but it was fragile (a typo compiled fine), slow-ish, and made
   exhaustive handling unverifiable by the compiler. The 6 screens with
   string-keyed cursors (Scale, Mixer, Mods, Instrument, Project, Effects —
   the other 6 screens already used plain `int` row/col grid cursors and were
   out of scope) now use a per-screen `enum class CursorId : uint8_t` instead;
   `NavNode` (`src/ui/ui_types.h`) became `template<typename CursorId> struct
   NavNode`. Procedurally-indexed fields (Scale's `NOTE_EN_0..11`/
   `NOTE_OFFSET_0..11`, Mixer's `TRK_VOL_0..7`) are contiguous enum ranges
   with arithmetic index recovery instead of string parsing. Mods/InstMod —
   the one screen with a genuinely 2-D index (quadrant 0-3 × param-slot 1-4)
   — enumerates all 16 `(param-slot, quadrant)` combinations as flat
   `MOD_P{1..4}_{0..3}` values in one contiguous block, with `QuadrantOf`/
   `ParamSlotOf` helpers replacing `fieldId.back() - '0'` and
   `fieldId.find(...)`. Instrument's Sampler/Macrosyn layouts share one enum
   (`InstrumentCursorId.h`) covering the union of both variants' fields.
   Scale's pre-existing `KEY`/interactive-fields-map mismatch (KEY is
   nav-reachable and resolvable but was never in the interactive-fields map,
   so it never got a cursor box) was preserved exactly rather than fixed
   incidentally by this port. NavNode maps are still rebuilt by value on
   Mods' hot path (unavoidable — they depend on live per-quadrant mod type);
   only the key type changed, not that behavior.

5. **Event-ring churn.** `tickTrack` emits `NOTE_OFF` unconditionally every
   tick for every inactive/silent track — even when no note is playing (most
   call sites don't check `isActive()`). At 8 tracks this floods the 1024-slot
   event ring with no-op events and forces consumers to filter. Emit only on
   actual state change. Relatedly, the identical 8-line `EngineEvent e_off{...}`
   block is copy-pasted ~9 times in `Engine.cpp`; it begs for
   `emitNoteOff(track)`.
   **[FIXED — CODE_CLEANUP_SPEC #5: `emitNoteOff` extracted (9→1), all
   previously-unguarded churn sites now check `isActive()` — 54% fewer events
   on the 40s demo render (6,504→2,975), audio verified bit-identical]**

6. **`e_on.instrument` is set to the sample handle, not the instrument index**
   (`Engine.cpp:403`: `... ? m_state.pendingInst[t]->sampler.sample : 0`).
   For macrosynth notes it's always 0. Any consumer treating this field as
   "instrument number" (its name) will be wrong. Either fix it to
   `m_trackInstrument[t]` or rename the field.
   **[FIXED — CODE_CLEANUP_SPEC #6]**

7. **`std::rand()` in the RANDOM LFO shape** (`Lfo.h:69`). Not
   audio-thread-appropriate (may lock, global state, non-deterministic across
   runs — which also breaks the "offline render is reproducible ground truth"
   property). Use a per-LFO xorshift like the demo-sample generators already do.
   **[FIXED — CODE_CLEANUP_SPEC #7: per-`Lfo` xorshift32, seeded in `trigger()`;
   verified bit-identical across independent runs, both at the `Lfo`-class level
   (M21) and a full-engine render (M22)]**

8. **Dead/duplicated DSP code in `SynthVoice::renderSample`.** The
   `postFilter` true/false branches are byte-identical (lines 189–206), and the
   degrade → amp → limiter → filter chain is duplicated wholesale between the
   sampler path and the macrosyn path. `rateScale` is computed by the
   mod-to-mod destinations and then **never used** — MOD_RATE/MOD_BINV silently
   do nothing to rates. `m_finished` is set but never read.
   **[PARTIAL — CODE_CLEANUP_SPEC #8: the dead `rateScale` array and its three
   write sites removed (honest no-op instead of looking implemented); the
   `postFilter` duplicate branches and the sampler/macrosyn DSP-chain
   duplication are untouched — not in scope for #8, still open]**

9. **Repo hygiene.** `build/`, `build_render/` (including `.exe`, `.dll`,
   `.obj`, CMake caches), stray `Renderer.obj`, `stderr_*.txt` debug logs,
   `nul.wav`, and `m8-sdl3.zip.zip` are tracked in git. This bloats every
   clone, makes `git status` unreadable, and guarantees merge noise. Add a
   proper `.gitignore` and `git rm -r --cached` the artifacts. Also:
   `include(FetchContent)` appears twice in CMakeLists, and `BUILD_CLONE` has
   no `option()` declaration or documentation.
   **[FIXED — CODE_CLEANUP_SPEC #9: build tree untracked (1,821 files,
   `git rm --cached`, files kept on disk), duplicate `include(FetchContent)`
   removed, `option(BUILD_CLONE ...)` added]**

10. **Misleading comments and leftovers.** `kBusAtten = 1.0f` sits under a
    comment explaining why the bus is attenuated (it isn't); the first
    `sctx.isPlaying` lambda in `main.cpp` (~line 1221) is abandoned dead code
    replaced immediately below; `PhraseScreenLayout.h` ships ~180 lines of
    hard-coded mock grid data that the real renderer overwrites; the layout
    mock content ("F#5", "REP 08") reads as design-tool output committed as
    code. `ScriptCtxHelper` being `static` works only because there is exactly
    one runner — a real ctx object would be cleaner.
    **[PARTIAL — CODE_CLEANUP_SPEC #10: `kBusAtten` deleted (verified
    bit-identical audio), dead `isPlaying` lambda deleted, mock layout data
    documented (not trimmed) in Phrase/Chain/Song `*ScreenLayout.h`;
    `ScriptCtxHelper`'s `static` left alone — genuinely optional per the spec
    and not touched beyond the adjacent dead-lambda removal]**

11. **Minor engine correctness nits.**
    - `PLAY_START` sets both `currentPhrase` and `currentChain` to `targetId`
      regardless of mode; harmless today but subtle.
      **[FIXED — CODE_CLEANUP_SPEC #11a: each field now set only when the new
      mode actually reads it]**
    - `MixerState`/instrument fields are `int` where every consumer treats
      them as `uint8_t` bytes; `uint8_t` would document range and shrink state.
      **[SKIPPED — CODE_CLEANUP_SPEC #11b: checked the stated risk first —
      these fields aren't in the `EngineCommand` union and `.m8s` conversion
      is field-by-field, not a memcpy, so the specific risk this note named
      doesn't apply as described. Still a large mechanical sweep for a purely
      cosmetic gain with no bug fixed; skipped deliberately, not by oversight]**
    - The demo-song hat's noise generator recomputes the *previous* sample's
      hash each call (fine, but O(2×) for no reason).
      **[SKIPPED — CODE_CLEANUP_SPEC #11c: this is a one-time startup cost
      (~4,320 samples at `Engine` construction), not per-render-frame; a real
      fix means restructuring the stateless generator function-pointer
      convention shared by all four demo sounds, for an imperceptible win]**
    - `Playhead::activeCol` is packed with a 3-bit mask at bit 26 but the
      struct field is declared after `playMode` and never used by the UI.
      **[CORRECTED — CODE_CLEANUP_SPEC #11d: this claim was checked and found
      false. `activeCol` is read at `ChainScreen.cpp:104` to pick which
      track's playhead to consult for the CHAIN-mode playback indicator. No
      fix applied — the original critique was simply wrong on this point]**
    - `SamplerState::samplePath` (128 bytes) duplicates `SampleData::path`;
      two sources of truth for the same string.
      **[DOCUMENTED, NOT REMOVED — CODE_CLEANUP_SPEC #11e: traced every
      read/write site; this is a legitimate second source of truth (the
      instrument's own record of intent, independent of the pool slot's
      lifecycle, read by `m8_render`'s `printTrackInfo`), not accidental
      duplication — it never round-trips through `.m8s` at all. Added a
      comment on the field explaining why. **[FIXED 2026-07-17]** The deeper
      problem the earlier note only half-saw: `convertSongToEngine` never
      populated `samplePath` for **loaded** songs at all (only the in-code demo
      kit set it), so every loaded sampler showed "(none)" and `m8_render` —
      which keys sample loading off `samplePath` — could load **no** sampler
      sample. Now copied from `m8::Sampler::sample_path` in the sampler branch
      of `convertSongToEngine`; the sampler oracle renders correctly.

### 5.3 Risk register for future work

| If you are about to… | Beware |
|---|---|
| Implement scales / phrase-level VOL+PIT FX (tables are **done**) | Do it inside `tickTrack`/`doTick` on the audio thread using existing state only; times in ticks; add OfflineHost tests first (the suite's naming convention is `B*/M*/L*/A*` + spec ID). Tables already work this way — see `Engine::tickTable`. |
| Refine synth fidelity (Braids parity, FM/Wav tuning) | Per-voice DSP state lives in `SynthVoice`; **note the FM/Wav wavetables are lazily generated on first render** (`initFMWavetables`/`generateWavShape`) — that's a one-time fill of pre-sized member buffers, not a heap allocation, so it stays RT-safe, but reset any feedback/phase state on `LOAD_SONG`. Validate offline against `m8_spectrum`; the hardware A/B is the acceptance gate. |
| Add a new screen | Follow `screens/<name>/` pattern: Render function + Layout header + NavNode map; wire into `ViewManager::getViewAt`, `main.cpp` input + render dispatch, and add a `.m8script`. |
| Add a new persisted field | Extend *both* converters in `SongIO.cpp` (`convertSongToEngine` / `convertEngineToSong`) and confirm the byte-identical round-trip tests still pass. |
| Touch `CommandRing` / rings | Re-run the TSan config (`-DM8_SANITIZE=tsan`) and `test_rt_safety`; keep capacity a power of two; keep payloads POD. |
| "Clean up" the leaked-on-overflow GC pushes | Don't switch to blocking or freeing on the audio thread; the leak is a documented, deliberate trade. Enlarge the ring instead. |

---

## 6. Quick file map

```
src/engine/    Engine.{h,cpp}       orchestrator: rings, tick, mix, effects, demo song setup
               Sequencer.{h,cpp}    data grids + demo song content
               SeqTypes.h           Step/Chain/Song/Table/Groove PODs + 0xFF sentinels
               CommandRing.h        SPSC ring + EngineCommand/ParamID
               EngineEvents.h       24-byte POD event (NOTE_ON/OFF/ROW_ADVANCE/TICK)
               EngineStateUpdater.h shared UI/engine param application
               SynthVoice.{h,cpp}   per-track voice: mods, degrade/amp/lim/filter
               SamplerEngine.{h,cpp} phase-walking sample playback, loop/pingpong
               SamplePool.{h,cpp}   128-slot refcounted sample registry (RT-safe)
               Envelopes.h, Lfo.h, Modulation.h   modulator primitives
               InstrumentSamplerParams.h, dr_wav.h
src/io/        SongIO.{h,cpp}       .m8s ⇄ engine conversion, overlay save
src/analysis/  AudioMetrics.{h,cpp}, Fft.h   objective audio measurements (kissfft)
src/ui/        Renderer.{h,cpp}     SDL3 + 40×30 VRAM shadow grid + dumps
               ViewManager.{h,cpp}  Shift+Arrow screen graph + modal stack
               ScriptRunner.{h,cpp} .m8script DSL for headless UI testing
               FileBrowser.{h,cpp}  WAV/.m8s picker + dr_wav decode
               screens/<name>/      12 screens: Render fn + Layout + NavNode map
src/tools/     main_render.cpp      offline WAV+CSV renderer
               main_analyze.cpp     WAV metric gate / diff
               main_makeprobe.cpp   probe .m8s generator
               main_capture.cpp     hardware serial+USB-audio capture
src/main.cpp   the app: event loop, input, mirrors, persistence UI, script glue
tests/         Catch2 suite + OfflineHost + AllocGuard + ui/*.m8script
third_party/   m8-files-cxx (submodule), kissfft, miniaudio
```
