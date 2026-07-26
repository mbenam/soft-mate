# M8 App Automation Spec — CI-Gated, High-Coverage Clone Scripting

Turn the clone's **existing but underused** headless script harness into a first-class automation
system: every script gated in CI, the observability blind spots closed, the command/assertion
vocabulary rounded out for authoring *and* verification, coverage extended to the features other
agents just landed (Braids MacroSynth, FM/Wav/Hyper, Tables, FX), and the runner refactored so the
same script dialect can also drive the real device (the `CloneBackend` half of
`M8_DEVICE_CONTROL_SPEC.md`'s differential harness).

> **Why now.** The harness is genuinely good — headless, deterministic, VRAM-shadow assertions, a
> working offline render→analyze loop — but **only 1 of 13 scripts is actually in CI**
> (`test_ui_scripts.cpp` runs `groovetest` and nothing else). The engine grew four synth types and
> Tables with **zero UI-script coverage**. Automation that isn't gated doesn't protect anything.

> **Offline by nature.** Everything here runs `m8_clone --script … --headless` with no audio device
> and no window — deterministic and CI-safe. No hardware, unlike the device spec.

> **Status of this spec.** Tiers 1–3 are **DONE**. Tier 4 is **PARTIALLY DONE** (2026-07-17/18; see
> the Tier 4 block below for what shipped, what's blocked, and why). Tier 5 (golden snapshots) is
> **DONE** (2026-07-18). Tier 6 (backend abstraction) is **SUPERSEDED** — a concurrent session's
> independent `DeviceScriptRunner` already achieves the same goal a different way; see the Tier 6
> block. Tier 7 (fuzz automation) is **DONE** (2026-07-18) — see the Tier 7 block. Tier 1:
> `tests/test_ui_scripts.cpp`
> is a discovered runner (globs `tests/ui/*.m8script` + `tests/ui/manifest.txt`); all 13 scripts are
> CI-gated, up from 1. Two real bugs were caught and fixed in the process: a stale fixture in
> `task6_test.m8script` (chain-ref bytes from the old "Night Drive" boot song, never updated after
> the boot song switched to `songs/sunrise.m8s`), and a genuine `STATUS_ACCESS_VIOLATION` crash —
> `--headless` was creating a real GPU-backed `SDL_Renderer` behind its hidden window, and spawning
> 13 processes back-to-back raced the GPU driver; fixed by forcing `SDL_SOFTWARE_RENDERER` when
> hidden (`Renderer.cpp`). Tier 2: `ScriptAppContext` gained `getPlayheadState`/`getPlayheadRow`
> (reading `Engine::getPlayheadState`/`getPlayhead` directly, the same wait-free atomic the UI
> itself reads — not the shadow grid, which can't see the playhead line); new commands
> `checkpoint_playhead`, `assert_playhead_row`, `assert_playhead_advanced`, and `wait_until
> <predicate> [timeout_frames]` (predicates: `playing`, `stopped`, `playhead_row >=|== <n>`,
> `screen contains "<text>"`). `playhead.m8script` was rewritten to use them — proving the row
> *moved*, not just that a flag is set — and verified stable across 16 direct repeated runs plus 5
> full-`[ui]`-suite passes, all green (the ASan/Release race is retired). Along the way, discovered
> and worked around a real (pre-existing, not
> introduced by this spec) interaction: loading `TEST-FILE.m8s` shows a missing-samples overlay
> whose dismiss handler swallows the *next* keydown — including a scripted `play` — so
> `playhead.m8script` now uses `songs/sunrise.m8s` instead.
>
> Tier 3: added `assert_field <label> <value>` (reads the shadow grid, whole-token label match,
> value stops at the first run of ≥2 spaces — the M8 screens' column gap — or end of row; verified
> against real INSTRUMENT-screen fields CUTOFF/PAN/TYPE); `goto <SCREEN>` (SONG/CHAIN/PHRASE/
> INSTRUMENT/TABLE/PROJECT/GROOVE/INST_MOD/SCALE/INST_POOL/MIXER/EFFECTS — a multi-frame state
> machine, driven by `onFrameStart`, that normalizes to SONG via a grid-position-independent press
> sequence derived from `ViewManager.cpp`'s `getViewAt`, then walks to the target column/row, then
> `onFrameEnd` verifies the header); `assert_wav <file> <metric> <op> <value>` (inline
> `AudioMetrics`/`dr_wav` gate — peak/rms/crest/dc_l/dc_r/dc_worst/clipped/nonfinite/silence/
> mid_rms/side_rms/correlation, ops `< <= > >= == !=` — reusing `dr_wav.h`'s existing single
> implementation in `FileBrowser.cpp`, not redefined); and `repeat <n> { ... }` (load-time
> unrolling in `loadScript` — the body is parsed once via the existing `parseLine` and the resulting
> commands appended `n` times, so no new runtime state and every existing verb, including `goto`,
> works inside it unmodified). **A real bug was caught and fixed while building `goto`:** its
> normalize-to-SONG sequence pressed DOWN only twice, assuming that always bottoms out at the
> grid's y=-2 floor — true only when starting at y≤0. Landing on a y=1 or y=2 screen (e.g. GROOVE,
> SCALE) left the *next* `goto` undershooting the floor by exactly that much, corrupting its aim by
> the same offset (confirmed by reproduction: two chained `goto GROOVE` calls landed the second one
> on SCALE, one row off). Fixed by pressing DOWN up to 4 times (the true worst-case distance across
> the full y∈[-2,2] range); verified with 17 chained `goto` calls covering every screen, several
> revisited, all passing. All four commands verified directly (including deliberate-failure paths
> for `assert_field`/`assert_wav`) before the full 147-test suite was re-run clean.
>
> **Tier 4 (2026-07-17) — partially done.** Before writing any script, discovered that the plan's
> premise didn't hold for 3 of the 4 synth types: `InstrumentScreen.cpp:209` hard-clamps the UI's
> TYPE field to `[SAMPLER, MACROSYN]`, and only `InstrumentSamplerLayout.h`/
> `InstrumentMacrosynLayout.h` exist — HyperSynth/FMSynth/WavSynth have **no UI type selector or
> layout at all**. Separately, `TableScreen.h`'s own header says "Table has no edit mode wired up
> yet" — stale-but-true: the engine now *executes* tables (`tickTable()`) but the app still can't
> *author* one. Deeper still: `SongIO.cpp` has **zero references to `tables[`** on either the load
> or save path — table data in a real `.m8s` file is silently dropped, so there's no fixture-file
> route into table coverage either, only the (also missing) UI route.
>
> **Shipped, fully UI-authored, verified against real regressions:**
> - `synth_macrosynth.m8script` — MACROSYN is the one type the UI fully supports. Toggles
>   instrument 00 SAMPLER→MACROSYN, edits SHAPE/TIMBRE/COLOR, renders, asserts the audio is
>   finite/non-clipped/non-silent. Uses `songs/sunrise.m8s`'s existing phrase-00-row-0 note as a
>   documented, verified (not assumed) precondition.
> - `fx_roundtrip.m8script` — authors a `TBL` phrase FX (now selectable in the UI's FX cycle —
>   `UiEditHelpers.h`, fixed alongside L12/L13), saves, reloads, confirms it survives on screen.
> - `nav_all_screens.m8script` — `goto` to all 12 screens + `assert_no_overlap` on each. **Found
>   and fixed three real, previously-undetected bugs** (no prior test ever checked overlap broadly):
>   1. Both `InstrumentSamplerLayout.h` and `InstrumentMacrosynLayout.h` drew a dead `"SCP"` label
>      at (34,26) — a leftover from before `ViewManager::renderChrome` centralized the navigator
>      display (the old, superseded `main_stage1.cpp` prototype drew a similar `"SCPIT"` string
>      directly) — exactly overlapping `renderChrome`'s own S/C/P indicators on *every* Instrument
>      screen frame. Deleted (dead code, `renderChrome` already draws it correctly).
>   2. `GrooveScreenLayout.h`/`InstPoolScreenLayout.h`/`ModScreenLayout.h`/`ScaleScreenLayout.h`
>      each drew their own `"T>128"` tempo readout at (34,2), exactly overlapping
>      `renderChrome`'s global one (called unconditionally every frame, `main.cpp:741`, no per-
>      screen guard). Deleted from all four.
>   3. `MixerScreen.cpp`'s output-volume bar was hardcoded to column 12, which is track 4's slot
>      (`i*3` for `i=0..7`) — a genuine, not-cosmetic collision. Relocated to column 24 (past
>      track 7's slot, otherwise unused) in both the draw call and its paired value-text
>      coordinate. Separately, `DrawVerticalBar`'s background rect was full-height with the
>      foreground fill drawn *on top* of it — since every mixer volume defaults non-zero
>      (`out_vol=0xE0`, tracks similarly), this always double-wrote the filled region. Changed to
>      adjacent (background = only the unfilled portion) rather than overlapping rects — same
>      visual result, no needless overdraw. (MIXER's `assert_no_overlap` is still skipped in the
>      script: sub-cell-precision bar rendering will always touch one boundary cell from both
>      sides whenever the fill height isn't an exact multiple of 8px, which is most values — a
>      real limit of a cell-granularity check applied to pixel-precision graphics, not a bug.)
> - **A fourth, non-layout bug found and fixed en route:** the intuitive sequential idiom
>   `key X; key <DIR>; key X` (used by `edit.m8script`/`save_reload.m8script` for note entry)
>   does **not** edit Instrument-screen fields (TYPE, AMP, SHAPE, ...) — `editHeld` is only true
>   between X's own down/up events, and a sequential script spans three separate frames, so X's
>   up event (and `editHeld=false`) is already processed before the direction key is even pushed.
>   Confirmed by direct reproduction: `closed_loop_glitch.m8script`'s documented "crank instrument
>   00's AMP to max" sequence provably does not touch AMP at all — worse, it also lands on the
>   wrong screen entirely (see below) and has apparently been passing for an unrelated reason. The
>   correct idiom is the *compound* key syntax `key X+<DIR>` (pushes X-down, DIR-down, DIR-up,
>   X-up all in one frame, so `editHeld` is still true when the direction fires), repeated via
>   `repeat N { key X+<DIR> }` for multi-step changes — used throughout `synth_macrosynth.m8script`
>   and verified step-by-step (TYPE, SHAPE ×3, TIMBRE ×32, COLOR ×32 all confirmed via
>   `assert_field` before/after each edit).
>
> **Not fixed (flagged, out of scope for this pass):** `closed_loop_glitch.m8script`/
> `closed_loop_fix.m8script` both (a) use the broken sequential edit idiom above, and (b) load
> `TEST-FILE.m8s`, whose missing-samples overlay swallows the first post-load keydown (same root
> cause `playhead.m8script` hit in Tier 2), throwing off their navigation and landing them on
> `PHRASE 20` instead of the Instrument screen — confirmed by reproducing their exact key sequence
> with intermediate `dump_screen` calls. They currently pass `m8_analyze`'s DC/crest gate anyway,
> apparently by accident (some unrelated PHRASE-screen edit still produces glitchy audio). Also
> not fixed: `edit.m8script` has no explicit `load`, so its `key X; key UP; key X` sequence is
> vacuous against the current boot fixture (`songs/sunrise.m8s` already has C-4 at phrase 00 row 0
> before any edit runs — confirmed by tracing `dump_screen` after each keypress) — it passes for
> the same reason `task6_test.m8script` used to (Tier 1), not because the edit path works. Fixing
> all three needs the same two changes: the overlay-dismiss keypress (see `playhead.m8script`)
> and either the compound-key idiom (`closed_loop_*`) or an explicit `load` of an empty fixture
> (`edit.m8script`, matching `save_reload.m8script`'s already-correct pattern).
>
> **Fixture generator improved along the way:** `m8_makeprobe --type fmsynth`/`--type hypersynth`
> built silent-by-construction probes (FM: every operator `level=0`, mathematically zero output
> under any algorithm; HyperSynth: empty `default_chord`, so `SynthVoice.cpp` sees zero active
> notes regardless of the phrase note played) — fixed both to audible defaults (confirmed via
> render: FM peak 0.000→0.392, HyperSynth 0.000→0.514). Added a `--table-tick N` flag that also
> assigns table 0 (pre-populated with a distinctive `transpose=+12` on row 0) via a phrase `TBL`
> FX — but this could not be used for a working `tables.m8script`: the SongIO gap above means the
> generated fixture's table content is silently dropped on load (confirmed: two renders, one with
> `--table-tick 1` and one without, are byte-identical, `m8_analyze --diff` reports 0.0). Kept the
> flag (harmless, useful once table load/save exists) but did not add `tables.m8script`.
> `m8_makeprobe`'s own `verifyRoundTrip` also has a separate, pre-existing, unrelated bug (checks
> "is instrument 0 a MacroSynth" for every `--type` except `sampler`, never updated for the newer
> types) — noted, not fixed; doesn't affect the generated files' correctness, only the tool's own
> self-check.
>
> **Genuinely blocked, not attempted:** `synth_fm.m8script`/`synth_wav.m8script`/
> `synth_hyper.m8script` per the original plan (UI-authored coverage) are impossible until the
> Instrument-screen TYPE/layout gap is fixed — a real feature-development task, not an automation
> task. `tables.m8script` is blocked on the SongIO load/save gap for the same reason. Both are
> product gaps this pass surfaced, not automation-script decisions.
>
> **2026-07-18 — closed_loop_glitch/closed_loop_fix and edit.m8script fully addressed** (the three
> scripts flagged, not fixed, in the 2026-07-17 pass above). `edit.m8script`: added the missing
> explicit `load` (`V4EMPTY.m8s`, matching `save_reload.m8script`'s already-correct pattern) — it
> was vacuous, silently relying on `songs/sunrise.m8s` already having C-4 at the target cell.
> `closed_loop_glitch`/`closed_loop_fix`: fixing the two previously-flagged mechanical bugs
> (overlay-dismiss, compound-key idiom) surfaced **four more real, confirmed-by-reproduction
> findings**, each requiring the fix to go one layer deeper than the last:
> 1. The scripts' own navigation comment was wrong — `GetSamplerNavMap`'s `TRANSP.RIGHT` is
>    `TBL_TIC`, not `AMP`; `assert_field` didn't catch this because it reads whatever's displayed
>    *anywhere* on screen, not what the cursor is on. Real path traced via `dump_json`'s cursor
>    field at every step.
> 2. TEST-FILE.m8s's default song-row-0 drill-down lands on **instrument 01, a real WavSynth**
>    ("WAV") — and `InstrumentScreen.cpp`'s entire display layer (`ResolveInstrumentValue` etc.)
>    only branches on `isMac`, never `isHyp`/`isFm`/`isWav`, so it always reads/shows
>    `inst.sampler.*` regardless of the instrument's actual type. `EngineStateUpdater`'s edit
>    routing, separately, *does* check `isWav` and correctly writes `inst.wav.amp` — confirmed via
>    temporary instrumentation: `type=4 (WAVSYNTH) isWav=1`, `wav.amp` 0→1 while the displayed
>    `sampler.amp` stayed 0 forever. A real, broader product bug (the Instrument screen mishandles
>    *any* loaded Hyper/FM/Wav instrument's shared fields, not just when creating one — sharper
>    than the 2026-07-17 "no type selector" finding) — not fixed, flagged only.
> 3. Retargeted to instrument 02 ("MAC", MacroSynth — confirmed via the render event CSV as the
>    actual droning culprit `status.md` Known Issues already documents) and verified the edit is
>    now genuinely causal: unedited TEST-FILE already fails (crest 3.61 dB) but the AMP-0→FF crank
>    measurably worsens it (crest 1.95 dB, clipped 1→13) — no longer riding on coincidence.
> 4. `saveSong` refuses pre-4.0 files (confirmed: `assert_error contains "pre-4.0"` after any save
>    attempt on TEST-FILE) — the intentional, tested behavior `pre40_refuses.m8script` checks.
>    TEST-FILE.m8s is pre-4.0, so `closed_loop_glitch`'s `save artifacts/glitch.m8s` has *never*
>    actually written that file (silent no-op; the script doesn't `assert_no_error` after `save`).
>    `closed_loop_fix` was therefore never continuing from Phase 1 — `artifacts/glitch.m8s` was
>    stale content from an unrelated earlier run (confirmed: it showed `songs/sunrise.m8s`'s tempo
>    and instrument list, not TEST-FILE's). Made `closed_loop_fix` self-contained (loads TEST-FILE
>    fresh, replicates the crank itself) rather than depending on a save that can't happen.
>
> With the edit path now genuinely correct, a fifth finding closed the loop: **reducing instrument
> 02's AMP cannot bring the render back to PASS at any value.** The baseline already fails (a
> moderate AMP=0x40 only reaches crest 2.12 dB, nowhere near the 6 dB threshold from an
> already-failing start), and AMP can't even *silence* the instrument — `SynthVoice`'s amp formula
> has a unity-gain floor (`clamp(1 + amp/255*7, ...)`, so `AMP=0x00` still means 1× gain, and
> TEST-FILE's own unedited value is already 0x00). `manifest.txt`'s policy for `fixed.wav` changed
> from `analyze_pass` to `analyze_fail` to match reality — both scripts now honestly demonstrate a
> mechanically-correct, genuinely causal edit path, and both correctly produce a failing render,
> since TEST-FILE's droning MacroSynth cannot be fixed through this instrument's AMP at all. A real
> PASS demonstration needs a different mechanism (e.g. muting via the Mixer) or a different
> fixture — not attempted here. All three scripts verified individually and via the full 147-test
> suite (146/147 — the one failure, `test_grid_nav.m8script`, is unrelated in-progress work from a
> concurrent session, not authored by this pass).
>
> Tier 5 (golden snapshots) and Tier 7 (fuzz) are done; Tier 6 (backend abstraction) is superseded.
> See their sections below for details.

---

## 0. What exists today (the foundation)

`m8_clone --script FILE --headless --out-dir DIR` runs the **whole app** (real `Engine`,
`Renderer`, `SongIO`) with a hidden window, pumping the main loop frame-by-frame via synthetic
`SDL_PushEvent`s. Deterministic: no audio device, no OS events, `SDL_Delay(0)`, `engine.render()`
pumped manually each frame so commands are processed before assertions. Every draw call stamps a
`VirtualCell[30][40]` **shadow grid**, which is what makes headless screen assertions possible.
Exit codes: 0 pass / 1 assertion / 2 parse; auto-dumps the screen on failure.

**Command vocabulary** (`src/ui/ScriptRunner.{h,cpp}`):
- input: `key`, `hold`, `type`, `wait`
- transport: `play`, `stop`
- persistence: `load`, `save`, `set_sample_root`
- offline audio: `render <seconds> <file.wav>` (headless-only; refuses if a live stream exists)
- introspection: `dump_screen`, `dump_json`, `screenshot` (BMP)
- assertions: `assert_screen contains|not_contains|row N contains`, `assert_row_matches <regex>`,
  `assert_cell_color`, `assert_slider`, `assert_no_overlap`, `assert_playing`, `assert_stopped`,
  `assert_no_error`, `assert_error contains`, `assert_song_name`

**The app seam** — `ScriptAppContext` (function pointers into `main.cpp`): `isPlaying`, `hasError`,
`getErrorMessage`, `getSongName`, `loadSong`, `saveSong`, `setSampleRoot`, `renderOffline`,
`audioActive`. Note what's **missing**: no way to read the **playhead position** or read back a
**field value** from engine/sequencer state — the two gaps below.

**Assets:** 13 scripts in `tests/ui/*.m8script`; `tests/test_ui_scripts.cpp` (the lone CI gate);
`m8_analyze` (WAV numeric gates) and `m8_analyze --diff` for the render→analyze closed loop.

---

## 1. Goals / non-goals

**Goals**
1. **Gate every script in CI** — a discovered runner, not a hand-maintained list of one.
2. **Close the two observability gaps**: playhead advancement, and value read-back after an edit.
3. **Make waits condition-based**, not fixed frame counts — kills the timing flakiness.
4. **Cover the new engine features** (Braids/FM/Wav/Hyper/Tables/FX) with author→render→analyze
   scripts, so the audible work is actually protected.
5. **Refactor the runner into a backend-agnostic core** so the same dialect drives clone + device.
6. Keep the dialect **small and shared** with `.m8script` on the device side.

**Non-goals**
- Live-audio-in-the-loop analysis (needs a real device + loopback; stays manual — Task 5b).
- Mouse/coordinate input (the M8 is key-only).
- Replacing the Catch2 engine tests — this is UI/integration automation, complementary to them.

---

## 2. Gaps to close (grounded)

| # | Gap | Root cause | Fix (tier) |
|---|---|---|---|
| G1 | Only 1/13 scripts in CI | `test_ui_scripts.cpp` hardcodes `groovetest` | Discovered runner (T1) |
| G2 | Can't assert the playhead **advanced** | Playhead drawn via `drawLinePixel`, not in the shadow grid; no engine-playhead callback in `ScriptAppContext` | Read `engine.getPlayhead()` via a new callback (T2) |
| G3 | Timing flakiness (`playhead.m8script` ASan/Release split) | Fixed `wait N` frames hoping state settled; assert races command processing | `wait_until <predicate>` + document the synchronous-mode contract (T2) |
| G4 | Can't read a **field value** back after editing | No engine/sequencer read seam; asserts only see rendered glyphs | `assert_field` reading the shadow grid at a known cell, or an engine read-back callback (T3) |
| G5 | **Zero** script coverage for Braids/FM/Wav/Hyper/Tables/FX | New features landed without UI scripts | Feature scripts (T4) |
| G6 | No protection against silent **screen drift** | No golden snapshots of `dump_json` | Snapshot testing (T5) |
| G7 | Runner not reusable for the device backend | `ScriptRunner` is SDL/Renderer-coupled | Backend abstraction (T6) |

---

## 3. Tiered plan

### Tier 1 — Gate every script in CI (highest value, smallest change)

Replace the single hardcoded test with a **discovered runner**: a Catch2 `[ui]` test that globs
`tests/ui/*.m8script`, runs each via `m8_clone --script … --headless --out-dir test_out_ui`, and
asserts the documented exit code. Use `DYNAMIC_SECTION(scriptName)` so each script is an individually
named sub-case (failures point at the file).

Handle the few non-self-contained scripts with a tiny **manifest** (`tests/ui/manifest.txt`:
`name  expect  [recipe]`) rather than baking policy into filenames:
- most scripts: `expect=0`, self-contained.
- `closed_loop_glitch` / `closed_loop_fix`: `recipe=render+analyze` — the runner renders, then
  invokes `m8_analyze` and asserts FAIL then PASS respectively (Task 7, now automated not demoed).
- any intentionally-skipped script: `expect=skip` with a reason (keeps the list honest).

**Acceptance:** CI runs all 13 scripts; a deliberately-broken script turns the suite red at that
named sub-case. `groovetest`'s bespoke test is subsumed and removed.

### Tier 2 — Deterministic observability (playhead + condition waits)

**Playhead (G2).** Add `int (*getPlayheadRow)(void* ctx)` / `int (*getSongRow)(void* ctx)` to
`ScriptAppContext`, wired to the engine's wait-free packed playhead atomics (`engine.getPlayhead()`
— the same source the UI reads; do **not** try to scrape `drawLinePixel`). New assertions:
- `assert_playhead_row <n>` — exact row.
- `assert_playhead_advanced` — row changed since the last checkpoint (proves motion, not just
  "transport flag on", which is all `assert_playing` can claim today).

**Condition waits (G3).** Add `wait_until <predicate> [timeout_frames]`, where predicate is any
assertion expression (`playing`, `playhead_row >= n`, `screen contains "…"`, `stopped`). The frame
loop advances until the predicate holds or the timeout trips (exit 1 with the screen dumped). This
replaces `wait <N>; assert_playing` patterns that assume the audio/command timing.

**Determinism contract (document + enforce).** In headless script mode the app opens **no** audio
device, so `engine.render()` is pumped synchronously each frame and command ordering is
deterministic. Codify this as an invariant: *automation assertions run only in headless mode and
depend only on the synchronous pump — never on audio-callback timing.* Add a guard that refuses
`--script` assertions if a live stream exists (the `render` command already refuses; extend the
principle). This retires the ASan/Release race as an automation concern.

**Acceptance:** rewrite `playhead.m8script` to `play; wait_until playhead_row >= 4;
assert_playhead_advanced` — passes 100% across ASan **and** Release, repeated runs.

### Tier 3 — Author-and-verify vocabulary (kept minimal, shared with device)

Add only what closes real gaps; every verb must also make sense for the device backend (T6).
- `assert_field <screen-relative label> <value>` (G4): reads the value cell adjacent to a label in
  the shadow grid and compares — closes the "edit then confirm" loop without an engine back-channel.
  (If shadow-grid label lookup proves brittle, fall back to an engine `readParam` callback; prefer
  the grid so clone and device assert identically.)
- `goto <screen>` — convenience that presses the nav keys until `topHeader` matches (mirrors the
  device `gotoScreen`); pure sugar over `key` + `assert_screen`, but keeps scripts readable and
  backend-portable.
- `assert_wav <file> <check> <op> <value>` — inline numeric gate over a rendered WAV via the
  `AudioMetrics` library (peak/rms/crest/dc/centroid/pitch), so a render→assert loop lives in one
  script instead of a shell pipeline. (`m8_analyze` already exposes these; expose the same checks
  to the runner.)
- `repeat <n> { … }` — bounded loop for sweep-style scripts (e.g. step a param across values).

Everything else stays as-is; resist growing a general scripting language.

### Tier 4 — Coverage for the features that shipped uncovered (the real payoff)

**Status: partially done, 2026-07-17 — see the status block at the top of this doc for the full
writeup.** Original plan below, kept for reference; struck-through items were found to be
impossible as planned and are now tracked as product gaps, not automation tasks.

One script per area, each `navigate → edit via keys → render → assert audio + screen`:
- `synth_macrosynth.m8script` — **DONE.** INSTRUMENT screen, set type MACROSYN, edit `shape`/
  `timbre`/`color`, render, `assert_wav` non-silent/finite/non-clipped, `assert_field` confirms
  each value stuck. (Guards the Braids port.)
- ~~`synth_fm.m8script`, `synth_wav.m8script`, `synth_hyper.m8script`~~ — **blocked, not a script
  problem.** The Instrument screen has no TYPE selector or layout for these three types at all
  (`InstrumentScreen.cpp:209` clamps TYPE to `[SAMPLER, MACROSYN]`). Needs the UI gap fixed first.
- ~~`tables.m8script`~~ — **blocked, not a script problem.** `SongIO.cpp` never loads or saves
  table data (zero references to `tables[`), so there's no file-fixture route either, and the
  Table screen still has no edit mode. Needs both gaps fixed first.
- `fx_roundtrip.m8script` — **DONE** (TBL only; GRV/TIC follow the identical pattern, not
  separately scripted). Authors a phrase FX, `save`s, `load`s, confirms it survives on screen —
  the UI counterpart to L12/L13; also exercises the FX command cycle now extended to include
  TBL/GRV/TIC.
- `nav_all_screens.m8script` — **DONE**, and found 3 real layout bugs in the process (see status
  block): a dead duplicate "SCP" label, a duplicate "T>" tempo readout on 4 screens, and a genuine
  column collision + redundant-fill issue in the Mixer's volume bars. All fixed.

**Acceptance:** these run in the T1 discovered suite; each fails if its feature regresses (verified
by temporarily breaking the feature — confirmed for `nav_all_screens.m8script`'s overlap checks by
reproducing each bug live before fixing it).

### Tier 5 — Snapshot / golden testing (drift protection)

**Status: DONE (2026-07-18).** Built directly against the `VirtualCell` shadow grid rather than
`dump_json` (avoids a JSON dependency and matches exactly what the renderer actually wrote, one
cell at a time): `Renderer::writeGolden`/`compareGolden` (`src/ui/Renderer.{h,cpp}`) serialize/
compare `row col ch_hex fg bg slider bracket` per cell to a plain-text file. New command
`assert_matches_golden <name>` (`src/ui/ScriptRunner.{h,cpp}`) reads/writes
`tests/ui/golden/<name>.txt`; a new `--update-goldens` CLI flag (`src/main.cpp`) switches it to
regenerate mode. `snapshot_all_screens.m8script` walks all 12 screens via `goto` +
`assert_matches_golden`, with 12 committed goldens under `tests/ui/golden/`.

**Acceptance verified, not just claimed:** deliberately corrupted `SongScreen.cpp`'s title color by
one bit (`{255,60,60,255}` → `{254,60,60,255}`) and reran the snapshot script — it failed with
`ASSERT FAILED (assert_matches_golden 'song': cell [row=0 col=0] mismatch: fg expected=ff3c3cff
actual=fe3c3cff)`, naming the exact screen, cell, and field. Reverted the corruption (confirmed via
`git diff` showing empty output) before moving on.

### Tier 6 — bridge to the device spec

**Status: SUPERSEDED plan, goal achieved a different way (2026-07-18).** The original idea below
(refactor `ScriptRunner` behind a shared `Backend` C++ interface, `CloneBackend`/`DeviceBackend`)
assumed the device side would need to link against this runner. It didn't: the concurrent
`M8_DEVICE_CONTROL_SPEC.md` work built its own independent `DeviceScriptRunner`
(`src/tools/m8/DeviceScriptRunner.{h,cpp}`, Tier 4 of that spec) — a **separate parser for the same
`.m8script` text dialect**, not a shared C++ abstraction. This is arguably the better design: `m8_nav`
must stay engine/SDL-free (`status.md`'s audio-API-separation invariant), so a real shared `Backend`
interface would have had to either violate that or push a non-trivial abstraction layer into a tool
that has no use for 90% of it (`render`, `assert_wav`, `repeat` etc. only make sense where an engine
exists). Two independent parsers converging on one text format sidesteps that entirely, at the cost
of some duplicated parsing logic — a good trade here.

**Confirmed compatible, not just assumed:** `DeviceScriptRunner::execCommand` upper-cases every verb
before matching (`verb = toUpper(cmd.verb);`), so this runner's lowercase convention (`goto`, `key`,
`assert_screen`, ...) parses identically on both sides. The verb sets overlap substantially (`key`,
`hold`, `wait`, `goto`, `load`, `play`, `stop`, `dump_screen`, `dump_json`, `assert_screen`,
`assert_field`, `assert_playing`) — a script written for the clone using only that intersection
(e.g. `nav_all_screens.m8script`'s `goto` + `assert_no_overlap`... modulo `assert_no_overlap` itself
being clone-only, since "overlap" is a shadow-grid write-count concept with no SLIP-decode
equivalent) is structurally parseable by both runners without modification. Their own
`m8_diffcheck` (`src/tools/main_diffcheck.cpp`) already does the differential comparison this tier
wanted — device screen vs. a golden reference — fulfilling the tier's actual goal (shared dialect,
cross-target verification) without the planned refactor. No `Backend` interface built; none needed.
`ScriptRunner`/`Renderer` were left as they are (Tier 5's golden format is deliberately its own
thing — a per-cell glyph+color+slider+bracket grid keyed to the clone's specific `VirtualCell`
rendering model, not something a text-only SLIP decode could produce or consume; the device side's
own golden format, captured from real hardware with `hw_type`/`fw` fields, serves a different
purpose — device-vs-expectation, not clone-regression — and shouldn't be unified with this one).

<details><summary>Original plan (not built, superseded above)</summary>

Refactor `ScriptRunner` so parsing + sequencing are separate from the SDL/Renderer I/O behind a
`Backend` interface:
```cpp
struct Backend {
  virtual void press(Button, int holdMs) = 0;   // key / hold
  virtual void type(const std::string&) = 0;
  virtual const ScreenView& read() = 0;         // shadow grid (clone) or SLIP grid (device)
  virtual bool playing() = 0; virtual int playheadRow() = 0;
  virtual bool load(const std::string&) = 0; /* … save, render, … */
};
```
`CloneBackend` wraps the current SDL-event + VRAM-shadow path (this app). `DeviceBackend` (device
spec) wraps `M8Device`. `ScreenView` is a common read model (glyph grid + cursor + optional color)
that both produce.

**Acceptance:** every existing script runs unchanged through `CloneBackend`; the device team can
implement `DeviceBackend` against the same header without touching the runner.

</details>

### Tier 7 (stretch) — property / fuzz automation

**Status: DONE (2026-07-18).** `tests/test_ui_fuzz.cpp`, tagged `[fuzz][.]` (Catch2's leading-dot
hidden-tag convention — excluded from the default, tag-less run; invoke explicitly with
`m8_tests.exe "[fuzz]"`). A random-walk generator emits a script of `goto`/`key`/compound-edit
(`key X+<DIR>`)/`wait` steps from a fixed legal vocabulary, with `assert_no_overlap` +
`assert_no_error` after every step and a `render` + `assert_wav ... nonfinite == 0` /
`clipped == 0` tail. 5 independent 60-step runs per invocation, `std::mt19937` seeded from
`M8_FUZZ_SEED` (env var) or a fixed default — same reproducibility convention as
`test_sequencer_walk.cpp`'s existing `B4.9` sequencer fuzz. On failure the seed and generated
script path are printed and the script is left on disk for replay.

**A real false-positive was caught and fixed while first running this:** all 5 runs immediately
failed with `assert_no_overlap`, 100% reproducibly across different seeds — not a rare fuzz find,
a systematic one. Traced to `goto MIXER` being in the screen vocabulary: MIXER's volume bars
already have a known, documented cell-granularity overlap false-positive (see
`nav_all_screens.m8script`'s comment on the same issue, Tier 4) that plain `assert_no_overlap`
can't distinguish from a real bug. Fixed by excluding MIXER from the fuzzer's screen list — after
which all 5 runs, plus a separate `M8_FUZZ_SEED`-reproducibility check, passed clean. Verified the
default (no-filter) suite is unaffected: `m8_tests.exe` (no filter) shows 147 test cases before and
after, confirming `[.]` hiding works.

---

## 4. The discovered runner (design detail)

- **Discovery:** glob `tests/ui/*.m8script`; cross-reference `manifest.txt` for `expect`/`recipe`.
- **Isolation:** each script gets a fresh `m8_clone` process (already how it works) and a unique
  `--out-dir` subfolder so artifacts (dumps, WAVs, screenshots) don't collide; cleaned on pass,
  retained on fail for triage.
- **Recipes:** `render+analyze` recipes run the script, then `m8_analyze --json` on the produced WAV
  and assert the documented verdict (Task 7 automated). Prefer the inline `assert_wav` (T3) once it
  exists; keep the external form for the `--diff` cross-check (`m8_render` vs in-app render, the L9
  identity).
- **Parallelism:** processes are independent → run concurrently; cap by core count.
- **Reporting:** `DYNAMIC_SECTION` per script; on failure, surface the script's auto-dumped screen
  path in the assertion message.

---

## 5. Determinism contract (the invariant automation rests on)

1. Assertions run **only** in `--headless` script mode (no audio device → synchronous
   `engine.render()` pump → deterministic command ordering).
2. No assertion may depend on wall-clock or audio-callback timing; use `wait_until`, never a bare
   `wait N` before a state check.
3. The offline `render` path is bit-equivalent to `m8_render` (guarded by test L9 — cross-song
   contamination reset on `LOAD_SONG`); automation audio checks use it, not the live stream.
4. Golden snapshots exclude the playhead and (optionally) theme accent color so they're stable.

Any new automation feature must preserve these or it doesn't belong in CI.

---

## 6. File map

```
tests/test_ui_scripts.cpp         # DONE (T1): discovered runner (globs *.m8script + manifest)
tests/ui/manifest.txt             # DONE (T1): name  policy  (pass/skip/analyze_pass/analyze_fail/diff)
tests/ui/golden/*.txt             # DONE (T5): 12 snapshot baselines, one per screen (plain-text
                                  #   per-cell format, not JSON — see Renderer::writeGolden)
tests/ui/fixtures/*.m8s           # DONE (T4): probe fixtures (m8_makeprobe), for fmsynth/hypersynth
tests/ui/synth_macrosynth.m8script # DONE (T4): full UI-authored coverage
tests/ui/synth_fm.m8script        # BLOCKED (T4): no UI type selector for FMSynth, not written
tests/ui/synth_wav.m8script       # BLOCKED (T4): no UI type selector for WavSynth, not written
tests/ui/synth_hyper.m8script     # BLOCKED (T4): no UI type selector for HyperSynth, not written
tests/ui/tables.m8script          # BLOCKED (T4): SongIO has no table load/save, not written
tests/ui/fx_roundtrip.m8script    # DONE (T4): TBL author→save→load
tests/ui/nav_all_screens.m8script # DONE (T4): every screen + no-overlap; found/fixed 3 layout bugs
tests/ui/snapshot_all_screens.m8script  # DONE (T5): goto + assert_matches_golden, all 12 screens
tests/ui/playhead.m8script        # DONE (T2): rewritten to checkpoint_playhead/wait_until
src/ui/ScriptRunner.{h,cpp}       # DONE (T2): + wait_until, checkpoint_playhead, assert_playhead_*
                                  # DONE (T3): + assert_field, goto, assert_wav, repeat
src/ui/Renderer.cpp               # DONE (T1, unplanned): software renderer when hidden=true
                                  #   (fixed the GPU-driver crash the discovered runner surfaced)
src/ui/Backend.h                  # T6: SUPERSEDED, not built — see Tier 6 section (parallel
                                  #   session's DeviceScriptRunner achieves the same goal instead)
src/main.cpp                      # DONE (T2): getPlayheadState/getPlayheadRow callbacks wired
                                  #   (getSongRow not added — no script has needed it yet)
src/ui/screens/instrument/InstrumentSamplerLayout.h   # DONE (T4, unplanned): removed dead "SCP" overlap
src/ui/screens/instrument/InstrumentMacrosynLayout.h  # DONE (T4, unplanned): removed dead "SCP" overlap
src/ui/screens/groove/GrooveScreenLayout.h    # DONE (T4, unplanned): removed duplicate "T>" tempo text
src/ui/screens/inst_pool/InstPoolScreenLayout.h # DONE (T4, unplanned): removed duplicate "T>" tempo text
src/ui/screens/mods/ModScreenLayout.h         # DONE (T4, unplanned): removed duplicate "T>" tempo text
src/ui/screens/scale/ScaleScreenLayout.h      # DONE (T4, unplanned): removed duplicate "T>" tempo text
src/ui/screens/mixer/MixerScreen.cpp          # DONE (T4, unplanned): output-bar column fix + adjacent
                                              #   (not overlapping) background/foreground bar fill
src/ui/screens/mixer/MixerScreenLayout.h      # DONE (T4, unplanned): OUT_VOL value moved to match
src/tools/main_makeprobe.cpp      # DONE (T4, unplanned): fmsynth/hypersynth silent-by-default fix,
                                  #   + --table-tick flag (works at the file level; blocked end-to-end
                                  #   by the SongIO table gap above)
src/ui/Renderer.{h,cpp}           # DONE (T5): writeGolden/compareGolden (per-cell text snapshot)
src/main.cpp                      # DONE (T5): --update-goldens CLI flag
tests/test_ui_fuzz.cpp            # DONE (T7): random-walk fuzz, [fuzz][.] hidden Catch2 tag
```

`ScriptRunner`/`Backend` link the clone; the discovered runner shells out to the already-built
`m8_clone` and `m8_analyze` — no new engine coupling in the test.

---

## 7. Phasing & acceptance summary

| Tier | Deliverable | Acceptance |
|---|---|---|
| 1 | Discovered runner + manifest; closed-loop recipes automated — **DONE** | all 13 scripts gated; break-one → red at that sub-case |
| 2 | Playhead callbacks, `assert_playhead_*`, `wait_until`, determinism guard — **DONE** | `playhead.m8script` 100% across ASan+Release, repeated |
| 3 | `assert_field`, `goto`, `assert_wav`, `repeat` — **DONE** | an edit→read-back and a render→gate script pass without shell glue — verified directly (`assert_field` on real INSTRUMENT fields, `goto` across all 12 screens incl. a caught+fixed sequential-navigation bug, `assert_wav` pass+fail paths, `repeat` unrolling), full 147-test suite clean |
| 4 | Synth/Tables/FX/nav coverage scripts — **PARTIALLY DONE** (macrosynth/fx/nav shipped; fm/wav/hyper/tables blocked on real UI/SongIO gaps, not automation) | each fails when its feature is deliberately broken — confirmed for nav_all_screens by reproducing and fixing 3 real bugs live |
| 5 | `assert_matches_golden` + snapshot script + `--update-goldens` — **DONE** | a 1-char layout change fails a named golden diff — verified live (title-color corruption caught, named the exact cell) |
| 6 | `Backend` refactor; `CloneBackend` — **SUPERSEDED** | goal met a different way: parallel session's independent `DeviceScriptRunner` + `m8_diffcheck` already give shared dialect + cross-target diffing, without the planned refactor |
| 7 (stretch) | random-walk fuzz automation — **DONE** | nightly; seed-reproducible via `M8_FUZZ_SEED`; catches overlap/NaN — found and fixed one false-positive (MIXER sub-cell overlap) before landing clean |

Tier 1 is the immediate win and unblocks the rest; 1–5 and 7 are pure offline/CI value; 6's original
bridge-to-device goal is achieved by `M8_DEVICE_CONTROL_SPEC.md`'s own `DeviceScriptRunner` work
instead.

---

## 8. Risks

- **Shadow-grid label lookup for `assert_field`** may be brittle where value cells aren't adjacent to
  labels. Mitigate: prefer reading known (row,col) from the same `*ScreenLayout.h` the screens use;
  fall back to an engine `readParam` callback only if the grid can't express it.
- **Golden churn.** Snapshots can become noisy if theme/layout shifts often. Keep them structural
  (glyph+position), exclude color unless a test targets it, and provide `--update-goldens`.
- **Flakiness creep.** Any reintroduction of `wait N; assert` re-opens the race. Lint scripts: a bare
  `wait` immediately followed by a state assertion is a review smell — prefer `wait_until`.
- **Process-per-script cost.** 13→many scripts × process spawn is CI time; parallelize and keep
  scripts short. The determinism contract makes them fast (no real-time waits).
- **Backend abstraction scope creep** (T6): keep `ScreenView` minimal (glyph grid + cursor + optional
  color); resist modeling every device quirk in the shared type.

---

## 9. Relationship to other specs

- `M8_DEVICE_CONTROL_SPEC.md` — T6's `Backend`/`ScreenView` is exactly the seam that spec's
  differential harness (`m8_diffcheck`, clone vs device) plugs into; this spec builds the tested
  clone half.
- `M8_UI_HARNESS_SPEC.md` (archived) — the harness this spec matures from underused to gated.
- `FX_COMMANDS_SPEC.md` — T4's `fx_roundtrip.m8script` is the UI-path complement to L12/L13.
- `M8_AUDIO_ANALYSIS_SPEC.md` — `assert_wav` (T3) surfaces `AudioMetrics`/`m8_analyze` checks inside
  the runner, completing the in-app author→render→analyze loop.
```
