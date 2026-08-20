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

Tags (complete list, re-derived from `tests/*.cpp` 2026-08-12 — the old list here named
only the first ten): engine — `[tempo] [walk] [fx] [groove] [commands] [sample_pool]
[sampler] [modulation] [rt_safety] [demo] [tables] [io] [audio] [macrosynth] [hypersynth]
[fmsynth] [wavsynth] [output_stage] [mixer] [eq]`; UI/screens — `[ui] [fuzz] [scale] [render] [bundle]
[char_picker] [confirmation_dialog] [file_browser] [clean_phrases] [clean_inst] [inst_pool]
[project_tempo] [project_transpose] [project_scale] [project_groove] [project_quantize]
[project_inst_pool]`; device — `[hwdecode]`; docs — `[doc]`

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

## 3b. Driving real hardware

Use **`tools/m8drv/m8drv.py`** (docs: `docs/tools/m8drv.md`), which supervises
`m8_nav --serve`: one connection, per-command timeouts, and kill/restart/re-home
recovery so a stuck device never needs a human hand.

Do **not** drive the device with one `m8_nav --flag` invocation per command. Each
pays `open()`'s 500 ms `'E'`-then-`'R'` sleep plus a read floor — about a second of
dead time each — and, worse, that pattern *hid* a real bug: `findCursorCell` latched
onto stale accent cells, which only shows up on the second and later reads within one
connection, because `'R'` repaints the stale cells away
(`M8_DRIVER_BUGS.md` #24). Five driver bugs surfaced the moment the connection was
held open.

`m8drv batch` runs many commands through one connection. `probe <KEY>` and
`inspect` are the diagnostics — `inspect` is the only view of colour in the whole
toolchain, since `SemanticState` carries none.

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

INSTRUMENT byte map   MEASURED 2026-08-19 on fw 6.5.2 by loading a probe with a distinct
            signature byte in every parameter slot and reading the screen.
            Offsets are from the start of an instrument record (WavSynth; the
            Sampler agrees, its own fields shift the tail by one).
              +0F volume      NOT shown on the INSTRUMENT screen at all
              +1A amp_type -> AMP     (showed 22 when volume held 11)
              +1B amp_limit-> LIM     (showed 03)
              +1C mixer_pan -> PAN    (44)
              +1D mixer_dry -> DRY    (55)
              +1E mixer_chorus -> MFX (66)
              +1F mixer_delay  -> DEL (77)
              +20 mixer_reverb -> REV (88)
            ^ SongIO.cpp read `amp` from `volume` and `lim` from `amp_type` --
              every field one across. FIXED 2026-08-19; pinned by test L33.
              `volume` is a separate level control: carried through load and
              save so a save cannot zero it, but NOT applied by the voice --
              its curve is unmeasured. See status.md.

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

EQ TYPE     7, 0x00-0x06   (read off fw 6.5.0 by cycling the field)
            00 LOWCUT  01 LOWSHELF  02 BELL  03 BANDPASS
            04 HI.SHELF  05 HI.CUT  06 ALLPASS
            ^ the file library's EqType enum stops at 5 and clamps 6 to Bell.
              Decode the raw byte, not eq_type().
EQ MODE     5, 0x00-0x04   00 STEREO  01 MID  02 SIDE  03 LEFT  04 RIGHT
EQ byte     type = b & 0x7, mode = (b >> 5) & 0x7; bits 3-4 unknown, preserve them
EQ freq     16-bit, coarse byte high, in Hz.  EQ gain 16-bit signed, HUNDREDTHS of a dB
EQ Q        plain byte 0-99, Q = 10^((b-50)/50) -- measured off the device's curve,
            so the default of 50 is exactly Q 1.0

PAN law     near channel at UNITY, far channel attenuated LINEARLY. R/L == pan/0x80,
            measured to three decimals across 00/20/40/60/80. NOT constant-power.
            (Upper half by symmetry; 00 and 80 both measured.)
HYPER WIDTH unipolar: 00 = no spread, FF = maximum. Not bipolar around 0x80.
            At FF the side content is only ~-31 dB (side/mid ~= 0.029).
Stereo      the M8 plays a stereo SAMPLE with its image intact -- L-only in the
            file stays L-only out (side == mid, corr 0.0000).
Capture     keyjazz VELOCITY is the level lever, not OUTPUT VOL, which does not
            reach the USB tap at all. 0x7F clips; 0x40 gives peak ~0.43 clean.
            Clipping destroys stereo information -- it pushes the channels
            together, so a clipped capture can measure as mono when it is not.

SCALE record        46 bytes, NOT 42. mask u16 LE at +0, 12 offsets at +2 as
                    SIGNED 16-BIT LE HUNDREDTHS of a semitone, 16-byte name at
                    +26, four unmodelled bytes at +42.
                    16 records at V4_OFFSETS.scale (0x1AA7E); 16*46 = 736 ends
                    exactly where the EQ block starts. 42 is the trap -- it is
                    what the fields add up to, it decodes record 0 perfectly,
                    and it drifts 4 bytes per record after that. Names are
                    padded with 0xFF. The global KEY is one byte at 0xBB.
                    OFFSET -00.50 on the device saves as CE FF. Do NOT read the
                    pair as (whole semitone, cents) -- that is what the vendored
                    library does, it agrees only where the bytes are zero, and
                    it cannot express anything in (-1.00, 0.00).
                    Anchored by tests/fixtures/device_golden/scaleprobe.m8s.

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
- **`LIM` 06-08** (`POST:W1..W3`) alias to plain `POST` — `applyLimiter`'s `default:` branch
  is a hard clamp, identical to `CLIP`. Folding curves unknown and not hardware-verified.
  *(Corrected 2026-08-19: this entry said 05-08. `LIM 05` POST:AD has been implemented as
  `std::tanh(x)` for some time — genuinely distinct from `04`'s hard clamp — and `status.md`
  line 680 already said so. Verified against `SynthVoice::applyLimiter` before editing.)*
- **`FILTER` 06/07** (ZDF) alias to LP/HP. DaisySP has no ZDF SVF.
- **LFO shapes 0x0D-0x16** (Drunk family, `*Env` one-shots) alias to TRI.
- **`MOD BINV`** is a guess.
- **`MOD RATE`, and the rate half of `MOD BOTH`/`MOD BINV`, do nothing.** Only the amount
  half of mod-to-mod routing (`amtScale`) is applied in `SynthVoice::renderSample`; there is
  no per-slot modulation-rate scaling. Removed the dead code that computed an unused
  `rateScale` array rather than leave it looking implemented (`CODE_CLEANUP_SPEC.md` #8).
- **Tables** are edited by the UI and ignored by the engine.
- **The voice path is mono for the SYNTHS only.** The **sampler** is stereo end to end as of
  2026-08-14 (`Engine` calls `SynthVoice::renderFrame`); hardware reproduces a stereo sample's
  image intact, so summing it lost real information (`hw_findings.md` §UI-12). Every synth path
  still returns one value duplicated into both channels. HyperSynth computes a `width` spread
  and throws it away — measured at only ~-31 dB on hardware (§UI-11), which is why it was not
  worth doing first.
- **The ModFX chorus's stereo spread is a CHOICE, not a measurement.** Its two channels are
  `daisysp::ChorusEngine`s differing only in base delay (`kChorusDelayL/R` = 6.03 / 3.65 ms),
  and their LFO phases stay locked because DaisySP exposes no way to offset them. Same
  approximation class as the phaser sweep range and the flanger delay range. *(Until
  2026-08-14 this return was mono for every input — `daisysp::Chorus` Inits both of its
  engines identically and cross-pans them back to `L == R`. Fixed; `A7`'s `[!shouldfail]` is
  gone and `A17` pins the chorus's image on its own.)* Don't "fix" A7 by restoring its old
  centred-dry setup; that only ever passed because the retired constant-power pan law was
  asymmetric at centre.
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

A spec's final step is always "fold findings into reference docs, then archive this spec," and it can't be ticked while any other step is unticked.

`status.md` must reflect reality: what is implemented vs what is spec'd. Keep it honest.
