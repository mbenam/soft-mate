# m8_nav

**Source:** [`src/tools/main_nav.cpp`](../../src/tools/main_nav.cpp) (508 lines as of
2026-07-18), plus the library it drives: [`src/tools/m8/M8Device.{h,cpp}`](../../src/tools/m8/M8Device.h),
[`ScreenModel.h`](../../src/tools/m8/ScreenModel.h), [`Primitives.{h,cpp}`](../../src/tools/m8/Primitives.h),
[`Gestures.{h,cpp}`](../../src/tools/m8/Gestures.h), [`DeviceScriptRunner.{h,cpp}`](../../src/tools/m8/DeviceScriptRunner.h)
**Build target:** `m8_nav` (links the `m8_device` static library — see CMakeLists.txt)
**Category:** real-hardware device driver — the general-purpose "read the screen, press buttons,
verify what happened" tool for a real M8 headless. This is the most complex and most actively
developed tool in the project.
**Links:** `m8_device` only (`M8Device.cpp`, `Primitives.cpp`, `Gestures.cpp`,
`DeviceScriptRunner.cpp`). **No engine, no SDL, no audio** — this is a hard architectural
invariant (`status.md`). Windows/Win32 serial only; the non-Windows path is a stub that prints
"serial not implemented on this platform" and fails.

> **This tool is under active reliability-hardening work as of 2026-07-18**
> (`M8_DEVICE_CONTROL_SPEC.md` Tier 4.5, §6.5). The design/architecture description below is
> stable; the *specific reliability caveats* listed in Gotchas reflect what was true when this
> doc was written and may already be fixed — **check `M8_DEVICE_CONTROL_SPEC.md`'s top status
> block for the current, authoritative state** before trusting any specific "X is unreliable"
> claim below as still true.

## What it does

Decodes the M8 headless's serial *display* protocol (the same SLIP-framed draw-command stream
the official `m8c`/`m8-display-app` speaks: `0xFD` draw-char, `0xFE` rect-fill, `0xFF` sysinfo)
into a text+color grid, and provides the single output primitive (button press). Everything else
— navigating to a screen, moving a cursor to a field, editing a value, loading a file, running a
`.m8script` — is built as a **closed-loop, read-verify-act** cycle on top of that: never assume a
press worked, always re-read the actual screen and check.

This read-verify-act discipline exists because of two hard hardware constraints (see
`M8_DEVICE_CONTROL_SPEC.md` §10): the M8's ~150ms key auto-repeat defeats any open-loop "press N
times" sequence, and the device does **not** auto-home — it keeps whatever screen it was left on,
so every routine must work from an *unknown* starting state.

## Architecture (3 layers, per `M8_DEVICE_CONTROL_SPEC.md`)

1. **Perception + transport** (`M8Device.{h,cpp}`) — serial port, SLIP decoding, `ScreenGrid`
   (the decoded text/color/cursor model), the one output primitive (`press`).
2. **Verified primitives** (`Primitives.{h,cpp}`, `ScreenModel.h`) — `gotoScreen`, `moveCursorTo`/
   `moveCursorToGrid`, `readField`, `editValue`, `enterNote`, `clearCell`, `pressUntil`,
   `dismissModal`, assertions. Each is the read-verify-act loop with a typed pass/fail result
   that carries the decoded screen on failure.
3. **Recipes/scripts** (`DeviceScriptRunner.{h,cpp}`) — the `.m8script` text dialect (shared
   vocabulary with the clone's own offline script runner — see
   `M8_APP_AUTOMATION_SPEC.md`), executed via `--script`.

## CLI flags

| Flag | Default | Meaning |
|---|---|---|
| `--port <name>` | *(required)* | Serial port, e.g. `COM3`. |
| `--dump-screen` | *(implicit default if no other mode flag is given)* | Decode and print the current screen as text. |
| `--json <path>` | — | Also (or instead) write the decoded grid as JSON (cells with colors). |
| `--keys <list>` | — | Comma-separated key masks (decimal or `0x` hex) to press one at a time, dumping the screen after each. Manual/diagnostic mode. |
| `--load-file <name>` | — | Closed-loop load: navigate to PROJECT → LOAD PROJECT browser → find and select `<name>` → confirm. See Gotchas. |
| `--goto-screen <name>` | — | Navigate to a named screen (`SONG`, `CHAIN`, `PHRASE`, `INSTRUMENT`, `TABLE`, `PROJECT`, `GROOVE`, `MODS`, `SCALE`, `INST_POOL`, `MIXER`, `EFFECTS`, or a partial match). Prints the resulting screen. |
| `--read-field <name>` | — | Move cursor to a named field and print its current value. |
| `--record-frames <path>` | — | Record decoded cells to a simple binary format for `--record-duration` ms (see Gotchas — not a true SLIP recording). |
| `--record-duration <ms>` | `5000` | Duration for `--record-frames`. |
| `--pin-gestures <field>` | — | **Tier 2 gesture discovery.** Navigates to the field, tests 16 candidate key-mask combinations against it, reports which ones edited the value vs. moved the cursor vs. did nothing. This is how `hw_buttons.json` was originally populated — see below. |
| `--script <path>` | — | Run a `.m8script` file against the real device via `DeviceScriptRunner`. |
| `--hold-ms <n>` | `40` | Button hold duration per press. Automatically clamped to `15` if `--load-file` is also set and this would be `> 20`. |
| `--gap-ms <n>` | `120` | Delay between a `--keys` press and reading the resulting screen. |
| `--no-reset` | `false` | Skip the `'R'` reset-request on open (`openNoReset` instead of `open`). **Without a prior full-framebuffer resend, a fresh process's decoded grid can start empty or badly incomplete** — see Gotchas. |
| `--max-ms <n>` | `2000` | Max wait for a settled screen read. |
| `--settle-ms <n>` | `250` | Settle time for a screen read. |
| `--min-ms <n>` | `700` | Minimum wait before the first read after opening. |

If no mode flag is given at all, `--dump-screen` is implied. Unknown flags print `unknown arg:
<flag>` and exit 1. No `--port` prints full usage and exits 1.

## Screen names (for `--goto-screen`, and `.m8script`'s `goto` verb)

`SONG`, `CHAIN`, `PHRASE`, `INSTRUMENT` (or `INST`/`INST.`), `TABLE`, `PROJECT`, `GROOVE`, `MODS`
(or `MOD`), `SCALE`, `INST_POOL` (or `INSTPOOL`), `MIXER`, `EFFECTS`. Partial/substring matching
is attempted if the exact name doesn't resolve.

## `--pin-gestures`: how the edit gesture masks were discovered

Navigates to the named field's screen, moves the cursor to it, then tests 16 candidate key-mask
combinations (`SHIFT+UP/DOWN`, `EDIT+UP/DOWN/LEFT/RIGHT`, `OPT+UP/DOWN`, various 3-key combos,
plus the plain arrows for comparison) one at a time — before/after each, it reads the cursor's
label text and classifies the result as **EDITED** (same field, different value), **MOVED**
(cursor left the field entirely), or **same** (no visible effect). Restores state afterward by
navigating away and back. The confirmed masks get written into
[`hw_buttons.json`](../../hw_buttons.json) by hand (this mode reports candidates, it doesn't
write the file itself) — see [`Gestures.h`](../../src/tools/m8/Gestures.h) for how that file is
then loaded and used by `editValue`/`enterNote`/`clearCell`.

## `.m8script` verbs this driver supports (via `--script`)

`goto`, `cursor`, `set`, `note`, `key`, `hold`, `wait`, `load`, `play`, `stop`, `dump_screen`,
`dump_json`, `assert_screen`, `assert_field`, `assert_row_matches`, `assert_playing`. Verbs are
case-insensitive (`DeviceScriptRunner` upper-cases before matching). This is a **separate parser**
from the clone's own offline `ScriptRunner` (`src/ui/ScriptRunner.cpp`) — not shared code — but
deliberately uses the same lowercase text dialect, so a script written for one can run
unmodified against the other wherever their verb sets overlap. See
[`m8_diffcheck`](m8_diffcheck.md) for the tool that exploits this to diff device output against a
clone-generated golden reference.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Bad CLI usage; unknown screen name; `--goto-screen`/`--read-field`/`--pin-gestures` failure |
| 2 | Serial port open failure; script parse error |
| 3 | Nothing was decoded at all (`dev.grid().cells.empty()`) — device not connected/streaming |

`--load-file`'s own success/failure (`rc`) is printed (`LOADED`/`FAILED (rc=N)`) but check the
actual mode block — some modes return their `rc` from `main`, some don't (e.g. `--load-file`
without any other flag falls through to the default dump-screen path and returns `rc` from
`main`'s end, which may be `0` even on a load failure if nothing else set it — read the printed
`LOADED`/`FAILED` line, don't trust the exit code alone for this specific flag).

## Examples

```
# Just look at the current screen
m8_nav --port COM3 --dump-screen

# Navigate to the Instrument screen
m8_nav --port COM3 --goto-screen INSTRUMENT

# Read one field's value
m8_nav --port COM3 --read-field CUTOFF

# Load a file by name from the SD card
m8_nav --port COM3 --load-file probe_slice4

# Run a device script
m8_nav --port COM3 --script tests/hw/set_param.m8script

# Manual single-press diagnostics (comma-separated raw masks)
m8_nav --port COM3 --keys 0x14,0x14,0x20 --hold-ms 40 --settle-ms 500

# Discover which key combo edits a field
m8_nav --port COM3 --pin-gestures CUTOFF
```

## Key masks (for `--keys` and writing scripts by hand)

`LEFT=0x80, UP=0x40, DOWN=0x20, SHIFT=0x10, PLAY=0x08, RIGHT=0x04, OPT=0x02, EDIT=0x01`
(`Key::` namespace in `M8Device.h`, pinned on firmware 6.5.2, recorded in `hw_buttons.json`).
Combine with bitwise OR / addition — e.g. `SHIFT+RIGHT = 0x10+0x04 = 0x14 = 20`. **Compute these
by hand carefully** — see Gotchas; mixing up `RIGHT` (`0x04`, plain cursor move) with
`SHIFT+RIGHT` (`0x14`, screen-to-screen navigation) is an easy, silent, hard-to-notice mistake
that produces plausible-looking-but-wrong behavior rather than an error.

## Gotchas (from a full day of real-hardware driving, 2026-07-18 — check the spec for current status)

- **`--no-reset` without a preceding full read in the same process can decode almost nothing.**
  Each process's `ScreenGrid` starts empty; without `'R'` requesting a full framebuffer resend,
  a fresh process only sees whatever incremental draw events happen to arrive. Use `--no-reset`
  only within a single process that has already done a full read (e.g. a `--keys` sequence that
  presses several keys and dumps after each — the FIRST read in that process should still be a
  normal `--port` open without `--no-reset` unless you're deliberately continuing from a prior
  process's already-decoded state, which doesn't actually persist across processes anyway).
- **Screen/chain/table/phrase IDs are hex and can end in a letter (A-F)** — e.g. a loaded phrase
  might show as "PHRASE FC" on screen. This tripped up screen identification before a 2026-07-18
  fix (`identifyScreen`'s digit-stripping only handled 0-9). If `--goto-screen` reports "reached
  X instead of target" and X looks like it has a trailing hex letter, that's the symptom to check
  for regression on.
- **Multi-key masks are easy to get wrong by hand.** `key RIGHT` (plain cursor move within a
  screen) and `key SHIFT+RIGHT` (screen-to-screen navigation) look similar in a script but do
  completely different things — always double check `SHIFT | RIGHT = 0x14 = 20`, not `0x04 = 4`.
- **`--goto-screen`'s multi-hop route execution has shown real reliability issues** in live
  testing (landing on the wrong screen after 2+ hops, even after the hex-ID fix above) — this is
  exactly what `M8_DEVICE_CONTROL_SPEC.md` Tier 4.5 (§6.5.1) exists to fix. If a single
  `SHIFT+<dir>` press (verified with `--dump-screen` after) reliably reaches the same screen a
  multi-hop `--goto-screen` call fails to reach, that's the tier-4.5 bug, not a new one.
- **Modals often need more than one press to dismiss.** "LOSE CHANGES TO CURRENT SONG?" and
  "LOSE CHANGES TO INSTRUMENT?" have both been observed needing 2 EDIT presses where 1 appeared
  to have zero effect on the next read. A `dismissModal()` primitive (retry-until-gone) was added
  to `Primitives.cpp` specifically for this — if you're calling into `Primitives` directly rather
  than going through `--script`, use it rather than a single bare `press(EDIT)`.
- **`--load-file`'s automated browser navigation has failed outright in testing** (`rc=11`,
  "reach the LOAD PROJECT row" loop giving up). If it fails, you can always fall back to manual
  `--keys` sequences (see the source's `loadFile()` in `Primitives.cpp` for the exact row/column
  logic to replicate by hand) — this is a known rough edge, not necessarily fixed by the time
  you're reading this.
- **`--record-frames` does not record a true SLIP stream.** Despite the name, it dumps the
  *already-decoded* cell grid (position/char/colors) in a simple binary format at a fixed polling
  interval, not the raw serial bytes — it can't be replayed through the actual SLIP decoder, only
  through custom code that understands its own ad-hoc binary layout.
- **COM port is exclusive.** Only one process can hold it — if you're also running a display
  client (or another `m8_nav`/`m8_capture` process), disconnect it first.
