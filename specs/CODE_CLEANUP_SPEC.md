# M8-SDL3 — Code Cleanup Spec

Actionable fix list for the code critique in **`ARCHITECTURE.md` §5.2** (and the two nits from
§5.1/§6 worth tracking). Each item: **Defect → Fix → Acceptance → Status**. Work top-down —
items are ordered correctness-first, then cheap hygiene, then larger refactors.

**This file is the source of truth for fix status.** When you close an item:
1. Set its **Status** here to `DONE (YYYY-MM-DD — one line on how)`.
2. Annotate the matching `ARCHITECTURE.md` §5.2 entry with `**[FIXED — CODE_CLEANUP_SPEC #N]**`.
3. Update the progress table below.

**Global acceptance (every item):** the full suite still passes — now **99 test cases /
302,524 assertions** (grew from 96/294,329 as items #3 and #7 added regression tests) — run
from the repo root (`./build/Release/m8_tests.exe`, not from `build/`; `test_ui_scripts` shells
out CWD-relative). Do not weaken a test to make it pass (`AGENTS.md` §4). Items touching the
audio thread (#3, #5, #7, #8) must not violate the RT-safety invariants (`AGENTS.md` §6): no
alloc/free/lock/throw/`std::string` in `render()`; `B8.1` stays green. Verified for #3 and #5
under `build_asan` in addition to the Release suite.

---

## Progress

Tiers 1–3 (everything except the three large structural refactors) are done as of 2026-07-16.

| # | Item | Severity | Tier | Status |
|---|---|---|---|---|
| 3 | `EngineStateUpdater` latent OOB UB | correctness | 1 | **DONE (2026-07-16)** |
| 7 | `std::rand()` in RANDOM LFO | RT-safety / reproducibility | 1 | **DONE (2026-07-16)** |
| 8 | `rateScale` computed, never applied | silent feature gap | 1 | **DONE (2026-07-16 — option (b), honest stub)** |
| 6 | `e_on.instrument` held sample handle | correctness | 1 | **DONE (2026-07-16 — now `m_trackInstrument[t]`, fixed with the TEST-FILE DC bug)** |
| 9 | Repo hygiene (tracked build tree, dup CMake) | hygiene | 2 | **DONE (2026-07-16)** |
| 10 | Misleading comments / dead leftovers | hygiene | 2 | **DONE (2026-07-16)** |
| 5 | Event-ring churn + copy-pasted `e_off` | clarity / ring pressure | 3 | **DONE (2026-07-16 — both 5a and 5b)** |
| 11 | Minor engine correctness nits (5 sub-items) | polish | 3 | **DONE (2026-07-16 — 11a fixed, 11b/11c skipped deliberately, 11d was a false claim (corrected), 11e documented)** |
| 2 | Triplicated song-load block | dedup | 4 | **DONE (2026-07-16 — also fixed a latent `.original.clear()` save-breaking bug found while unifying)** |
| 1 | `main.cpp` god function | structure | 4 | **DONE (2026-07-16 — 1,386 → 817 lines)** |
| 4 | Stringly-typed UI navigation | structure | 4 | **DONE (2026-07-16 — 6 screens, enum `CursorId` per screen)** |

Tiers: **1** correctness (do first) · **2** cheap safe hygiene · **3** small cleanups ·
**4** larger refactors (most risk, do last, each optional).

---

# Tier 1 — Correctness

## #3 — `EngineStateUpdater` forms out-of-bounds references before the switch

**Defect.** `src/engine/EngineStateUpdater.h:11-13`:
```cpp
auto& inst  = state.instruments[cmd.targetId];  // vector<Instrument>, size 128
auto& mod   = inst.mods[cmd.row];               // Modulator[4]
auto& scale = state.scales[cmd.targetId];       // Scale[16]
```
These run for **every** `UPDATE_PARAM`, including mixer/effects params where `targetId`/`row`
are unrelated and unvalidated. A mixer command with `targetId = 100` forms `scales[100]`
(array of 16) — out-of-bounds pointer arithmetic, UB even though it's never written.
`test_rt_safety.cpp` actively sends `UPDATE_PARAM` with garbage `targetId`, so this is
exercised, not theoretical.

**Fix.** Form each reference **lazily inside the case(s) that use it**, guarded like the other
command handlers in `Engine::processCommands`. Concretely: delete the three eager `auto&`
lines; in the instrument/sampler/macrosyn/mod cases bounds-check
`0 <= targetId < instruments.size()` (and `0 <= row < 4` for mods) before touching `inst`/`mod`;
in the scale cases bounds-check `0 <= targetId < 16` before touching `scale`; `mx`/`fx` are
fine (no index). On an out-of-range index, `break` (drop the command) — matches the
bounds-checked drop pattern already used for `SET_STEP` et al. in `Engine.cpp`.

**Acceptance.**
- Full suite green, including `[rt_safety]`.
- Build `build_asan` (`-DM8_SANITIZE=asan`) and run `[rt_safety]` + `[commands]` — no ASan
  report. (`AGENTS.md` §3: ASan is warranted here, this touches memory access.)
- Add a `[commands]` regression: push `UPDATE_PARAM` with `paramId = MIX_MIX_VOL` and
  `targetId = 200`, apply through `EngineStateUpdater::applyParameterUpdate`, assert the mixer
  value changed and nothing else was touched (no crash, no scale write).

**Status.** **DONE (2026-07-16).** Restructured `applyParameterUpdate` into three groups: the
instrument/sampler/macrosyn/mod params share one bounds-check on `targetId` (with mods further
bounds-checking `row` against `mods[4]` inside that group), and the scale params share one
bounds-check on `targetId` against `scales[16]` (with the two per-note params further
bounds-checking `row` against `notes[12]`). Mixer/effects/BPM params never index anything.
Added regression `B6.10` (`tests/test_commands.cpp`) covering all three groups with
out-of-range indices — no crash, valid state untouched, subsequent valid updates still work.
Verified under `build_asan` (`[rt_safety]` + `[commands]`) — no report.

---

## #7 — `std::rand()` in the RANDOM LFO shape

**Defect.** `src/engine/Lfo.h:69` (case `0x0C`) calls `std::rand()` on the audio thread.
`std::rand()` may lock, uses hidden global state (not per-voice), and is non-deterministic
across runs — which breaks the "offline render is reproducible ground truth" property that
`m8_render` / `m8_spectrum` / the `[audio]` tests rely on.

**Fix.** Replace with a per-`Lfo` xorshift PRNG (same style as the demo-sample generators in
`Engine.h`). Add a `uint32_t m_rngState` member seeded to a fixed non-zero constant in the
LFO's reset/`trigger()` (deterministic per note-on), advance it inline in the `0x0C` case.
No allocation, no lock, no shared state.

**Acceptance.**
- Full suite green (`[modulation]` covers LFO shapes; the M18 fuzz test still passes).
- Determinism check: `m8_render --phrase <one using a RANDOM-LFO instrument> --out a` twice →
  `m8_analyze --diff a.wav a2.wav` reports `max |A-B| = 0.0` (byte-identical across runs). With
  `std::rand()` this diverges; with the seeded PRNG it must not.

**Status.** **DONE (2026-07-16).** Added `uint32_t m_rngState` (default `0x9E3779B9u`),
reseeded in `trigger()`; case `0x0C` advances a xorshift32 (same formula as the existing
demo-sample noise generators) instead of calling `std::rand()`. Added regression `M21`
(`tests/test_modulation.cpp`) — two independently-triggered `Lfo` instances fed the same
shape/freq/trig sequence produce bit-identical output — and `M22`, the full-engine version:
two independent `OfflineHost` renders of the same phrase through a RANDOM-LFO-modulated
instrument are `memcmp`-identical. Both pass.

---

## #8 — `rateScale` is computed by mod-to-mod dests but never applied

**Defect.** `src/engine/SynthVoice.cpp:95` declares `rateScale[4]`; lines 139-141
(`MOD_RATE` / `MOD_BOTH` / `MOD_BINV`) write it; **nothing ever reads it**. So MOD RATE and the
rate half of MOD BOTH/BINV are silent no-ops — the code looks like it implements them but
doesn't. `amtScale` (the amount half) *is* applied, so the asymmetry is invisible without
reading closely.

**Fix — pick one, and say which in the Status line:**
- **(a) Implement it** (the real fix): apply `rateScale[i]` to slot `i`'s LFO frequency —
  scale the `freq` argument passed to `m_lfo[i].process(...)`. This is hardware-parity
  behaviour, so verify against the modulation spec / a capture before claiming parity. Larger
  scope; only take this if MOD RATE parity is actually being worked (`AGENTS.md` §5: don't
  gold-plate).
- **(b) Honest stub** (default): remove the dead `rateScale` writes and the array, and add
  MOD RATE / MOD BOTH-rate / MOD BINV-rate to the placeholder list in `AGENTS.md` §8 and
  `status.md` (MOD BINV is already noted there as "a guess"). This removes the misleading
  code without pretending a feature exists.

**Acceptance.**
- Full suite green (`[modulation]`).
- (a): a test showing MOD RATE actually shifts the target LFO's period; (b): `grep rateScale
  src/engine/SynthVoice.cpp` returns nothing, and the placeholder docs mention it.

**Status.** **DONE (2026-07-16 — took option (b)).** No hardware capture exists yet to verify
what real MOD RATE scaling should even look like, so implementing (a) would just be another
guess layered on `MOD BINV`'s existing one. Removed the `rateScale` array and its three write
sites; `MOD_RATE` is now an explicit no-op case with a comment. Documented in `AGENTS.md` §8
and `status.md`'s Placeholders section. `grep rateScale src/engine/SynthVoice.cpp` → no
matches. Full suite unchanged at 302,524 assertions (confirms nothing was exercising the dead
writes).

---

## #6 — `e_on.instrument` held the sample handle, not the instrument index

**Defect (historical).** `Engine.cpp` set `e_on.instrument` to `sampler.sample` (a
`SampleHandle`; `0` for macrosynth, `255` for a sampler with no sample) — wrong for any
consumer reading the field by its name.

**Fix (applied).** `Engine.cpp:403` now `e_on.instrument = static_cast<uint8_t>(m_trackInstrument[t]);`.

**Status.** **DONE (2026-07-16 — fixed alongside the TEST-FILE DC-drone bug; see
`archive/BUG_TESTFILE_DC_DRONE.md` §4.3).**

---

# Tier 2 — Cheap, safe hygiene

## #9 — Repo hygiene

**Defect.** Three parts:
1. **~1,821 build files are tracked in git** (`build/`, `build_render/` — `.exe`, `.dll`,
   `.obj`, CMake caches, `.tlog`s). Confirmed: `git ls-files | grep -E '^build/|^build_render/'`.
   This bloats every clone and drowns `git status`.
2. `include(FetchContent)` appears **twice** in `CMakeLists.txt` (lines 20 and 150).
3. `BUILD_CLONE` gates targets but has no `option()` declaration or documentation.

**Already done (2026-07-16 cleanup):** root-level stray artifacts removed (`Renderer.obj`,
`stderr_*.txt`, `nul.wav`, `nul_events.csv`, `m8-sdl3.zip.zip`, `artifacts/`, `test_out_ui/`);
`.gitignore` extended (`build_asan/`, `*.zip`, `artifacts/`, `test_out_ui/`). The tracked
build tree and the CMake nits remain.

**Fix.**
1. `git rm -r --cached build build_render` (untrack, keep on disk). They're already in
   `.gitignore`, so they stay ignored after. Commit as an isolated "stop tracking build output"
   change so the huge deletion doesn't pollute a functional diff.
2. Delete the duplicate `include(FetchContent)` (keep the first, line 20).
3. Add `option(BUILD_CLONE "Build the SDL3 app + fetch SDL3" ON)` near the top with a one-line
   comment on what it gates (SDL3 fetch + `m8_clone`; off = engine/tools/tests only).

**Acceptance.**
- `git ls-files | grep -E '^build/|^build_render/'` → empty.
- `git status` shows only real source changes.
- Clean reconfigure from scratch still works: `cmake -B build -A x64 && cmake --build build
  --config Release --target m8_tests` succeeds; suite green.

**Status.** **DONE (2026-07-16).** `git rm -r --cached build build_render` (1,821 files
untracked from the index; files remain on disk, both already gitignored so this is staged-only
and reversible until committed). Removed the duplicate `include(FetchContent)` (kept the one at
the top). Added `option(BUILD_CLONE "Build the SDL3 app (m8_clone)" ON)` — matches the existing
cached value in all three build trees (`UNINITIALIZED=ON`, i.e. previously set via `-D` on the
command line), so this is a no-op for existing configures. Verified: clean reconfigure of
`build/` succeeds, `m8_tests` and `m8_clone` both still build, full suite green.

---

## #10 — Misleading comments and dead leftovers

**Defect.**
- `Engine.cpp:570` `constexpr float kBusAtten = 1.0f;` under a comment explaining why the bus
  is attenuated — it isn't (1.0 = no-op). Comment lies. (Cross-ref `status.md` known issue
  "Bus attenuation 1.0".)
- `main.cpp` ~1221: the first `sctx.isPlaying` lambda is abandoned dead code, replaced by the
  capture-friendly `ScriptCtxHelper` version immediately below.
- `PhraseScreenLayout.h` (and peers) ship ~180 lines of hard-coded mock grid data ("F#5",
  "REP 08") that the real renderer overwrites — reads as design-tool output committed as code.
- `ScriptCtxHelper` is `static` and works only because there's exactly one runner.

**Fix (each independent, all low-risk):**
- Either make the bus attenuation real (give `kBusAtten` a <1.0 value and keep the comment) or
  delete the constant + comment and rely on mixer headroom. **Decide via a render check**:
  `m8_render --song` then `m8_analyze` — if crest/clip are already fine, delete it (don't
  change audio output silently); if changing it, note the before/after metrics. Prefer delete
  unless you intend to change the mix.
- Delete the dead first `isPlaying` lambda in `main.cpp`.
- For the mock layout data: confirm it's overwritten at runtime (it is — the render functions
  repopulate), then reduce each `*ScreenLayout.h` to the structure the renderer actually needs,
  or add a header comment stating the literals are placeholders the renderer overwrites. Prefer
  the comment if trimming risks touching layout geometry the renderer reads (verify with a
  `nav.m8script` run after).
- `ScriptCtxHelper`: optional — pass a real context object instead of the `static`. Low value;
  only do it if touching that code anyway (`AGENTS.md` §5).

**Acceptance.**
- Suite green; `nav.m8script` + a phrase-screen script still pass (layout untouched
  functionally).
- If `kBusAtten` changed: record `m8_analyze` peak/crest before and after in the Status line.

**Status.** **DONE (2026-07-16).** `m8_analyze` on the 40s demo song before touching anything:
crest 10.78 dB, 0 clipped, peak 0.899 — already well clear of the hard gates, so deleted
`kBusAtten` per the "prefer delete" rule rather than silently changing the mix. Confirmed via
`m8_analyze --diff` that removing the ×1.0 multiply produced bit-identical output
(`max |A-B| = 0.000000000`). Deleted the dead first `isPlaying` lambda in `main.cpp` (confirmed
via grep it was never read — `sctx`, built separately below, is what's actually passed to
`onFrameEnd`). Added a documenting comment (not a trim) to `PhraseScreenLayout.h`,
`ChainScreenLayout.h`, and `SongScreenLayout.h` — traced each screen's render loop and confirmed
the interactive-grid `.text`/`.normal_color` literals are genuinely dead (only `.col`/`.row`/
`.selected_color` are read; the displayed value always comes from live `Sequencer` state).
`ScriptCtxHelper`'s `static` left alone — didn't touch that code beyond the adjacent dead-lambda
deletion, and it's explicitly optional per this spec. **Incidental fix found while verifying**:
running all UI scripts surfaced that `save_reload.m8script` now failed (`SAVE FAILED`) — caused
by the earlier repo-cleanup session deleting `artifacts/`, which this checked-in script saves
into, exposing a real latent gap where `m8::io::saveSong`'s `writeFile` never created its parent
directory. Fixed in `src/io/SongIO.cpp` (`std::filesystem::create_directories` on the parent
path before opening the `ofstream`). All 12 `tests/ui/*.m8script` scripts pass; full suite
unchanged.

---

# Tier 3 — Small cleanups

## #5 — Event-ring churn and copy-pasted `e_off` blocks

**Defect.** The identical 8-line `EngineEvent e_off{...}; e_off.type = NOTE_OFF; …; emit(e_off);`
block appears ~9× in `Engine.cpp`. Several sites emit `NOTE_OFF` **unconditionally** every tick
for silent/inactive tracks (some paths guard with `isActive()`, some don't — inconsistent),
flooding the 1024-slot ring with no-op events.

**Fix — two steps, keep them separate:**
- **5a (safe, do first):** extract `void Engine::emitNoteOff(int t)` that fills and emits the
  event from `m_state.play*Row[t]` + `m_frameCounter`. Replace all ~9 copies. Pure refactor,
  **no semantic change** — the same events are emitted.
- **5b (semantic, verify carefully):** make emission conditional on actual state change (only
  emit when a voice was active / had emitted a NOTE_ON). **Risk:** `[commands]` tests (e.g.
  B6.7 "engine-initiated stop is visible") and `noteOffsForTrack` in `OfflineHost` assert on
  NOTE_OFF presence/count. Run `[commands]` + `[walk]` after and reconcile any count changes —
  if a test legitimately expected a spurious off, fix the *code* semantics and update the test
  with a one-line note (`AGENTS.md` §4), don't just delete assertions.

**Acceptance.**
- 5a: suite byte-for-byte green, `Engine.cpp` has one `emitNoteOff`, zero inline `e_off` blocks.
- 5b: suite green; add/adjust a `[commands]` test asserting no NOTE_OFF is emitted for a track
  that never played a note in a given phrase.

**Status.** **DONE (2026-07-16 — both steps).** 5a: added `Engine::emitNoteOff(int t,
int songRowOverride = -1)` (the override handles the two sites that use `m_songRow` directly
instead of `m_state.playSongRow[t]`); replaced all 9 inline blocks. Verified bit-identical via
`m8_analyze --diff` on the 40s demo song and full suite green (302,524 assertions, same as
before the refactor). 5b: traced which of the 9 sites are genuine per-tick churn vs. one-shot
row-boundary events — only the "`t != activeCol`" site (PHRASE/CHAIN mode) sits outside the
`playTick[t] == 0` gate and fires every tick; the rest fire at most once per row. Guarded all 5
previously-unguarded call sites with `isActive()`, matching the pattern the other 4 sites
already used. Reasoned through why this is safe before touching anything: `SynthVoice::noteOff`
on an already-inactive voice is a no-op (`m_gateTarget` already 0, ADSR `gate(false)` only acts
during `Sustain`), and every existing test that inspects `NOTE_OFF` counts does so for tracks
that had a real, currently-active note — confirmed by full suite staying at exactly 302,524
assertions post-change (including `B4.11` "chain end fires NOTE_OFF" and the KIL tests in
`test_fx.cpp`). Real-world effect on the 40s demo render: **6,504 → 2,975 events** (54% fewer)
for identical, bit-for-bit-verified audio output. Verified under `build_asan` (full suite) and
all 12 `tests/ui/*.m8script` scripts.

---

## #11 — Minor engine correctness nits

Five independent sub-items; each tiny. Tick them off individually in the Status line.

- **11a** `PLAY_START` sets both `currentPhrase` and `currentChain = targetId` regardless of
  mode. Harmless today. **Fix:** set only the field the mode uses. Acceptance: `[walk]` green.
- **11b** `MixerState`/instrument fields are `int` where consumers treat them as `uint8_t`.
  **Fix:** narrow to `uint8_t` to document range + shrink state. **Risk:** these cross the
  `CommandRing` inside POD structs and are `memcpy`'d for `.m8s` round-trip — changing widths
  changes struct layout. Verify `[io]` byte-identical round-trip (L4) still passes and
  `static_assert(std::is_trivially_copyable...)` holds. **Higher-risk than it looks — do it
  deliberately or skip.**
- **11c** Demo hat noise generator recomputes the previous sample's hash each call (O(2×)).
  **Fix:** cache. Cosmetic; demo-only path. Acceptance: `[demo]` + A1/A2 audio metrics
  unchanged.
- **11d** `Playhead::activeCol` is packed (3 bits at bit 26) but never read by the UI.
  **Fix:** either consume it where the UI needs the active column, or drop it from the packing
  and struct. Acceptance: `[commands]` playhead-packing round-trip (B6.9) still green.
- **11e** `SamplerState::samplePath` (128 B) duplicates `SampleData::path` — two sources of
  truth. **Fix:** decide the owner (instrument config vs pool slot) and reference rather than
  copy, OR document why both exist (the instrument must remember its path even when no sample
  is loaded — likely legitimate). Prefer documenting if removal risks the load/save path.

**Status.** **DONE (2026-07-16 — 11a ✅ · 11b skipped · 11c skipped · 11d corrected · 11e ✅).**

- **11a** Fixed: `PLAY_START` now sets `currentPhrase` only when the new mode is `PHRASE`,
  `currentChain` only when it's `CHAIN` (SONG uses neither — confirmed by grepping every read
  site; both fields are read exactly once each, in `tickTrack`, each gated to its own mode).
  `[walk]` green (9,968 assertions), full suite unchanged.
- **11b** Skipped, deliberately. Checked the stated risk first: `MixerState`/`SamplerState`/
  etc. are **not** in the `EngineCommand` union (only `Step`/`ChainStep`/`SampleData`/
  `SongPayload` are), and `SongIO.cpp` converts field-by-field with implicit conversions, not a
  memcpy of these structs — so the specific POD/`.m8s`-round-trip risk this spec worried about
  doesn't actually apply as described. But narrowing is still a ~40-field mechanical sweep
  across `Engine.h` plus every arithmetic/comparison call site in `main.cpp`'s `pushParam`/
  `ModifyValue` and the UI screens, for a purely cosmetic gain (documents range, shrinks state)
  with zero bug fixed. Per this spec's own "do it deliberately or skip" and `AGENTS.md` §5,
  skipping.
- **11c** Skipped, deliberately. This runs once at `Engine` construction (building the demo hat
  sample buffer, ~4,320 samples), not per audio-render-frame — a genuinely negligible one-time
  cost. A real fix requires restructuring `install()`'s generator calling convention (currently
  a stateless `float(*)(float,float)` function pointer shared by all four demo sounds) to thread
  the previous sample forward, which risks the kick/snare/clap generators too for an
  imperceptible startup-time win. Skipping rather than touching shared demo-generator plumbing
  for this.
- **11d** The critique's premise was checked and found **false**: `Playhead::activeCol` **is**
  read by the UI — `src/ui/screens/chain/ChainScreen.cpp:104` uses it to pick which track's
  playhead to consult for the CHAIN-mode playback indicator. No fix applied; corrected the claim
  in `ARCHITECTURE.md` rather than force an unnecessary change. `B6.9` (playhead-packing
  round-trip) unaffected either way.
- **11e** Documented, not removed. Traced every read/write site: `samplePath` is set only by the
  `LOAD_SAMPLE` command handler and never touched by `SongIO.cpp`'s `.m8s` conversion (it doesn't
  round-trip through the file at all — the library's own `sample_path` field survives via
  save-by-overlay, completely independent). This is a legitimate second source of truth — the
  instrument's own record of intent, read by `m8_render`'s `printTrackInfo` without touching the
  pool — not accidental duplication. Added a comment on the field in `Engine.h` explaining this.
  **Found in passing** (out of scope for this item, flagged as a separate task): `samplePath` is
  never populated for samples that fail to load, so `printTrackInfo` shows "(none)" instead of
  the actual missing path even though the `.m8s` file specifies one — confirmed with
  `TEST-FILE.m8s`'s missing TR505 sample.

---

# Tier 4 — Larger refactors (most risk; each optional, do last)

> These are structural. `AGENTS.md` §5: don't refactor unasked and don't add abstractions
> "for later." Only take a Tier-4 item when it directly serves work you're doing anyway, or
> when explicitly greenlit. Each must land behind a green suite + a UI script pass.

## #2 — Triplicated song-load block

**Defect.** The ~35-line "PLAY_STOP → pack `[Sequencer][EngineState]` buffer → LOAD_SONG →
update UI mirrors → build missing-samples message" sequence appears **3×** in `main.cpp`
(file-browser branches ~438 and ~518 — one likely unreachable since the FILE_BROWSER view
`continue`s in the first branch — plus the key-up branch ~1063) and a 4th time in the
`ScriptRunner` `loadSong` lambda. ~120 lines of near-duplicate, a divergence-bug farm.

**Fix.** One helper: `bool loadSongIntoEngine(const std::string& path, ...)` taking the command
sink + UI mirror refs + the missing-samples out-param, returning success. Call it from all four
sites. Also delete the confirmed-unreachable branch (verify with a `--script` load test first).

**Acceptance.** `load_each.m8script`, `save_reload.m8script`, `missing_samples.m8script`,
`pre40_refuses.m8script` all still pass. `main.cpp` shrinks ~120 lines; one load path.

**Status.** **DONE (2026-07-16).** Added `loadSongIntoEngine(path, sampleRoot, commandRing,
uiSequencer, uiEngineState, currentSongPath, currentLoadResult, missingSamplesMsg)` as a free
function. Confirmed the suspected-unreachable branch actually was dead — traced it precisely:
`ViewManager::handleNavigation` returns `false` immediately with no side effects whenever a
modal is active (`ViewManager.cpp:43`), and the reachable FILE_BROWSER block unconditionally
`continue`s, so `getCurrentView()` can never be `FILE_BROWSER` at the point the second branch
checked for it — deleted outright rather than routed through the helper. Replaced the two
reachable interactive-browser sites and the `ScriptRunner` `sctx.loadSong` lambda (down to a
3-line pass-through). **Caught a real bug while unifying**: the two interactive-browser sites
called `currentLoadResult.original.clear()` after loading; the `ScriptRunner` path never did.
`saveSong()` (`SongIO.cpp`) needs `origin.original` to re-parse — clearing it breaks any save
that follows a load. Confirmed empirically: unifying on the clearing behavior broke
`save_reload.m8script` (which exercises the `ScriptRunner` path exclusively) immediately.
No script exercises "interactive-browser load then save," so this was very likely a pre-existing
latent bug in the two interactive sites that nothing had ever caught — fixed by unifying on the
**non-clearing** behavior instead (the one path that was actually tested end-to-end). All 12
`tests/ui/*.m8script` scripts pass; full suite unchanged (302,524 assertions). `main.cpp` net
~150 lines shorter. **Noted, not chased further**: `save_reload.m8script` (and, in the prior
session, `load_each.m8script`) crash intermittently (~5–10%) under the Release SDL harness but
0/20 under `build_asan` — same signature and mechanism as the already-documented
`playhead.m8script` race in `status.md` (ASan's heavier instrumentation changes the timing
window). Pre-existing, unrelated to this change — the pure-engine suite has been 100% reliable
throughout.

---

## #1 — `main.cpp` god function

**Defect.** ~1,386-line `main()` holds input for all 12 screens, persistence UI, overlays,
script glue, and the audio callback. The per-screen `if (view == X)` hold-to-edit chains are
near-duplicated 12×. Screens *render* via free functions but *input* has no equivalent.

**Fix.** Introduce a per-screen input entry point mirroring the existing render free-functions:
`handleInput(view-state&, event, editHeld, commandSink&)` per `screens/<name>/`. Move each
screen's input chain out of `main()` into its module. Keep it mechanical — move, don't
redesign; one screen at a time, suite + that screen's `.m8script` green after each.

**Acceptance.** All `tests/ui/*.m8script` pass. `main()` materially shorter; each screen's
input lives beside its render/layout. No behaviour change (scripts are the guard).

**Status.** **DONE (2026-07-16).** `main.cpp`: **1,386 → 817 lines** (41% reduction). Added a
`HandleXInput` (+ `HandleXEditRelease` where the screen has X-release behavior) function to all
12 `screens/<name>/` pairs, mirroring the existing `RenderXScreen` convention exactly. Moved
mechanically — no logic redesign, string-cursor/NavNode-map mechanism kept as-is (see #4 below).
Shared infrastructure extracted to two new headers: `src/ui/UiCommands.h` (`CommandSink`,
`PushParam` — `main()`'s local `CommandSink` struct and `pushParam` lambda both removed, now
dead) and `src/ui/UiEditHelpers.h` (`AdjustU8`/`AdjustS8`/`ModifyValue`/`InsertDefault` — the 4
free functions removed from `main.cpp`, now dead there too). PROJECT and INSTRUMENT needed a
small screen-scoped `ProjectActionState` struct to thread the file-browser/text-input/save-path
state their LOAD/SAVE/SAMPLE ROOT actions touch, beyond their own cursor.

**One real bug caught while extracting** (not introduced by it — traced back through the
original inline code): the KEY_DOWN/ENTER path for `PROJ_SAVE` sets a `"SAVED: ..."` message on
success; the X-release path historically only reported failure. Preserved as-is with a comment
rather than "fixed" into new behavior — not this item's job to change functionality.

**Verification** (this is the highest-risk change in the whole spec, verified accordingly):
intermediate build after each batch of screens, one comprehensive final pass rather than
per-screen script runs given 12 screens' worth of tool-call overhead. Full suite green
(302,524 assertions, unchanged) under both Release and `build_asan`. **All 12**
`tests/ui/*.m8script` scripts pass under **both** Release and `build_asan` — nav, phrase/chain/
song/groove editing, save/reload, missing-samples, pre-4.0 refusal, playhead, and both
closed-loop glitch/fix scripts. No ASan report.

---

## #4 — Stringly-typed UI navigation

**Defect.** Cursor positions are `std::string` IDs (`"TRK_VOL_0"`, `active_cursor_mod.back()
- '0'`); NavNode maps are rebuilt by value on every keypress. Typos compile; exhaustiveness is
uncheckable; minor per-key overhead.

**Fix.** Replace the string IDs with per-screen `enum class` cursor targets + a static
nav/adjust table (built once, not per keypress). This pairs naturally with #1 (do #1 first, or
together per screen). Large surface — every screen's layout + main.cpp dispatch.

**Acceptance.** All UI scripts pass (they assert on-screen text/positions, so a nav regression
shows up). No visible behaviour change.

**Status.** **DONE (2026-07-16).** Scoped to the 6 screens that actually used string-keyed
`NavNode` cursors: Scale, Mixer, Mods/InstMod, Instrument, Project, Effects. (The other 6
screens -- Song, Chain, Phrase, Groove, Table, InstPool -- already used plain `int` row/col grid
cursors with no NavNode map; out of scope, nothing to convert.)

`NavNode` (`src/ui/ui_types.h`) became `template<typename CursorId> struct NavNode`. Each of the
6 screens got its own `enum class CursorId : uint8_t` (with a `NONE` sentinel replacing the old
`""` "no neighbor" convention) in its `*ScreenLayout.h` (Instrument's Sampler/Macrosyn variants
share one enum via a new `InstrumentCursorId.h`, since ~17 of their ~30 fields are common to
both). Every `GetXNavMap`/`GetXInteractiveFields`/`ResolveXValue`/`HandleXInput` site switched
from string comparison (`==`, `.find(...)`) to enum comparison.

Two screens had procedurally-generated field IDs that needed an index-recovery scheme instead of
string parsing:
- **Scale** (`NOTE_EN_0..11`, `NOTE_OFFSET_0..11`) and **Mixer** (`TRK_VOL_0..7`): each pattern
  is a contiguous block of enumerators; index recovery is `static_cast<int>(id) -
  static_cast<int>(id_0)` instead of `substr`/`back()-'0'`.
- **Mods/InstMod** (the hardest case): field IDs encode *two* independent runtime indices
  (quadrant 0-3 × param-slot 1-4, where the valid param-slot range is itself mod-type-dependent).
  All 16 `(param-slot, quadrant)` combinations are enumerated as one contiguous
  `MOD_P1_0..MOD_P4_3` block; `ParamCursor(pIdx, q)` / `ParamSlotOf(id)` / `QuadrantOf(id)`
  replace the old `"MOD_P" + pIdx + "_" + q` construction and `fieldId.back()-'0'` /
  `fieldId.find("MOD_P1")` recovery. `GetModNavMap`/`GetModInteractiveFields` are still rebuilt
  by value on every keypress/frame (unavoidable -- they depend on the instrument's live
  per-quadrant mod type); only the key type changed, not that pre-existing rebuild cost.

Scale's pre-existing `KEY` / interactive-fields-map mismatch (`KEY` is nav-reachable and
resolvable via `ResolveScaleValue` but was never present in `GetScaleInteractiveFields`, so it
silently never gets a cursor box) was found during the port and preserved exactly, not fixed
incidentally -- documented inline in `ScaleScreenLayout.h`.

**Verification:** Release build clean (0 errors) across all 6 screens plus `main.cpp`'s 6 cursor
variable declarations. Full suite green (302,524 assertions, 99 cases, unchanged) under both
Release and `build_asan`. All 12 `tests/ui/*.m8script` scripts pass under both Release and
`build_asan` (rc=0, no ASan report) -- nav, edit, save/reload, groove, missing-samples,
pre-4.0 refusal, playhead, live-vs-offline, load-each, and both closed-loop glitch/fix scripts,
covering every converted screen's nav graph and value editing.

---

## Notes

- **Ordering rationale:** #3/#7 are genuine UB / invariant violations (fix now); #8 is a silent
  feature gap; #9/#10 are near-free and shrink noise; #5/#11 are small correctness/clarity;
  #2/#1/#4 are the big structural wins but carry the most regression risk, so they come last and
  each stays behind the UI-script safety net.
- **Don't batch unrelated items into one commit** — one item per commit keeps each fix
  bisectable and lets Status here track reality.
- Items #3, #5, #7, #8 touch the audio thread — re-read `AGENTS.md` §6 before each, and run
  `build_asan` for #3.
