# AGENTS.md

Working agreement for this repo. Read this before doing anything.

This is a personal project. The goal is a working M8 tracker clone, not a certified
product. Ship something that runs, then make it correct. Not the other way round.

---

## 1. Build the app. Always.

### Build directories — there are exactly two. Never create more.

    build/         normal build (Release)
    build_asan/    ASan build (Release)

Do not create build_x86, build_tsan, build_debug, build_test, build2, or anything else.
If a build directory is misconfigured, DELETE it and reconfigure — do not sidestep it by
making a new one:

    Remove-Item -Recurse -Force build
    cmake -B build -A x64

Configure once, then only ever build into it:

    cmake -B build -A x64
    cmake -B build_asan -A x64 -DM8_SANITIZE=asan

Build with --target, always. Never a bare `cmake --build build`:

    cmake --build build --config Release --target m8_clone
    cmake --build build --config Release --target m8_render
    cmake --build build --config Release --target m8_tests

If you think you need a third build directory, stop and say why. You almost certainly don't.


## 2. Test output discipline

The test suite exists to catch regressions, not to generate reading material.

**While iterating on a fix**, run only the relevant tag or test, compact:

```powershell
.\build\Release\m8_tests.exe "[sampler]" --reporter compact
.\build\Release\m8_tests.exe "B4.9*"     --reporter compact
```

`--reporter compact` prints one line per failure and nothing on success. Use
`--reporter console` only to get detail on a specific failure you are chasing.

**Run the full suite once**, at the end, when you believe the work is complete.

**Never paste test output into a reply.** Report pass/fail counts and the text of actual
failures. Nothing else.

Tags: `[tempo] [walk] [fx] [groove] [commands] [sample_pool] [sampler] [modulation]
[rt_safety] [demo]`

Never write a test that asserts per-sample in a loop. Accumulate a flag, assert once.

---

## 3. Platform: Windows / MSVC only

Do not use WSL. Do not use Linux. Do not use TSan or UBSan — MSVC does not have them.

ASan is available and works:

```powershell
cmake -B build_asan -A x64 -DM8_SANITIZE=asan
cmake --build build_asan --config Release --target m8_tests
```

Run ASan when the change touches memory ownership (the sample pool, the GC ring, the
command ring). Skip it otherwise. It is not a ritual.

If porting becomes necessary later, we will cross that bridge then.

---

## 4. Report honestly

Past reports in this project have been wrong in ways the code contradicted. Do not let
that happen again.

- **Never claim a test passed unless you ran it.** If a sanitizer was not enabled, say so
  plainly. Do not describe the harness's own `REQUIRE`s as "UBSan".
- **Never claim a fix landed in a file you did not edit.** Cite the file and line.
- **If you contradict something you said earlier, say so.** Do not quietly change the story.
- **Do not weaken a test to make it pass.** If a test is wrong, fix the test and say
  explicitly that you changed the assertion and why. If the test is right and the code is
  wrong, fix the code.
- When asked for evidence, paste the command and its output. Not a summary of it.

---

## 5. Scope discipline

- **Do what was asked. Do not gold-plate.** If you think something adjacent is broken, say
  so in one line and move on. Do not fix it unasked.
- **Do not refactor code you were not asked to touch.**
- **Do not add abstractions "for later".** Later is not here.
- If a task is genuinely blocked on a decision, ask one question and stop. Do not guess and
  build on the guess.

---

## 6. Architecture invariants — do not break these

These were expensive to establish. They are not negotiable without discussion.

**The audio thread must never allocate, free, throw, lock, or touch a `std::string`.**
Everything the audio thread reads is either engine-owned or arrives through the
`CommandRing`. Test `B8.1` enforces this by counting allocations inside `render()` — if it
goes red, you broke it.

**The engine has zero SDL dependencies.** `m8_engine` is a separate target that does not
link SDL. If `m8_tests` fails to link because something under `src/engine/` includes SDL,
that is a bug in the engine, not in CMake.

**The UI never reads engine state directly.** It holds shadow copies (`uiSequencer`,
`uiEngineState`) and pushes commands. `Engine::getState()` was deleted deliberately. Do not
bring it back.

**Sequencer data is POD.** No `std::string`, no `std::vector`, in anything the audio thread
reads. This is what makes the ring, the memcmp tests, and the file round-trip possible.

**Sample buffers are owned by the pool, refcounted, keyed by path.** Voices hold non-owning
pointers. Frees happen on the UI thread via the GC ring, never on the audio thread.

---

## 7. Hardware-verified constants — do not "improve" these

These were captured from a real M8 headless over the display protocol. They are correct.
If your instinct says a value looks wrong, your instinct is wrong.

```
PLAY        15 modes, 0x00-0x0E
            00 FWD      01 REV      02 FWDLOOP  03 REVLOOP  04 FWD PP
            05 REV PP   06 OSC      07 OSC REV  08 OSC PP   09 REPITCH
            0A REP.REV  0B REP.PP   0C REP.BPM  0D BPM.REV  0E BPM.PP

FILTER      8 modes,  0x00-0x07
            00 OFF  01 LOWPASS  02 HIGHPAS  03 BANDPAS
            04 BANDSTP  05 LP>HP  06 ZDF LP  07 ZDF HP

LIM         9 modes,  0x00-0x08
            00 CLIP  01 SIN  02 FOLD  03 WRAP
            04 POST  05 POST:AD  06 POST:W1  07 POST:W2  08 POST:W3

MOD TYPE    6, 0x00-0x05
            00 AHD ENV  01 ADSR ENV  02 DRUM ENV  03 LFO  04 TRIG ENV  05 TRACKING

MOD DEST    14, 0x00-0x0D
            00 OFF     01 VOLUME   02 PITCH    03 LOOP ST  04 LENGTH
            05 DEGRADE 06 CUTOFF   07 RES      08 AMP      09 PAN
            0A MOD AMT 0B MOD RATE 0C MOD BOTH 0D MOD BINV
            ^ CUTOFF is 0x06. It is NOT 0x03.

MOD AMT     bipolar, 0x80 = neutral, 0x00 = full inverted, 0xFF = full positive
LFO TRIG    00 FREE  01 RETRIG  02 HOLD  03 ONCE
TRACK SRC   00 NOTE  01 VELOCITY  02 VEL. TAKE
TRIG SRC    an instrument index (sidechain source)

Sampler root note   C-4 (MIDI 60)
DETUNE              1/16 semitone per step, 0x80 centre
Envelope times      IN TICKS, tempo-relative. Not seconds.
LOOP window         [LOOP ST, LOOP ST + LENGTH], relative to the WHOLE SAMPLE
```

The last one is the only inference in the set. It is pinned by test `S6`. If `S6` ever goes
red, the fix is one line in `SamplerEngine::computeRegion()` — do not redesign around it.

---

## 8. Known placeholders — do not mistake these for finished work

- **`INST_MACROSYN` is a POLYBLEP saw**, not Braids. The oscillator models are not
  implemented. `shape`, `timbre`, `color`, `redux` are stored and ignored.
- **`PLAY` modes 09-0E** (REPITCH, BPM families) fall back to their non-repitched
  equivalents. They need SLICE first.
- **`LIM` 05-08** (`POST:AD`, `POST:W1..W3`) alias to plain `POST`. Curves unknown.
- **`FILTER` 06/07** (ZDF) alias to LP/HP. DaisySP has no ZDF SVF.
- **LFO shapes 0x0D-0x16** (Drunk family, `*Env` one-shots) alias to TRI.
- **`MOD BINV`** is a guess.
- **`MOD RATE`, and the rate half of `MOD BOTH`/`MOD BINV`, do nothing.** Only the amount
  half of mod-to-mod routing (`amtScale`) is applied in `SynthVoice::renderSample`; there is
  no per-slot modulation-rate scaling. Removed the dead code that computed an unused
  `rateScale` array rather than leave it looking implemented (`CODE_CLEANUP_SPEC.md` #8).
- **Tables** are edited by the UI and ignored by the engine.
- **The voice path is mono.** `SamplerEngine` reads both channels; `SynthVoice` sums them.
- **`loadDemoSong()` is scaffolding.** It disappears once `.m8s` loading works.

If you touch one of these, say so. Do not silently "fix" a placeholder into something else.

---

## 9. Specs

The specs in the repo are the source of truth for what to build:

- `M8_SAMPLER_SPEC_V2.md`
- `M8_MODULATION_SPEC.md`
- `M8_PERSISTENCE_SPEC.md`

Values in them were verified against hardware. Do not substitute your own. If a spec is
wrong, say so and stop — do not quietly diverge.

`status.md` must reflect reality: what is implemented vs what is spec'd. Keep it honest.
