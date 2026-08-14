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

> **Reliability status, 2026-08-14.** The Tier 4.5 caveats this doc carried from 2026-07-18 have
> largely been answered: multi-hop `--goto-screen` and `--load-file` both work (see Gotchas), and
> four further driver bugs were found and fixed by holding a single connection open rather than
> spawning a process per command (`M8_DRIVER_BUGS.md` #22-#26). **For anything multi-step, drive
> the `--serve` daemon** — one-shot invocations pay a ~1 s handshake each and, worse, hid bug #24
> entirely, because `open()`'s `'R'` repaints away the stale cells that make position reads go
> stale within a connection. [`m8drv`](m8drv.md) is the supervised client for that mode.

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
| `--semantic-state` | — | Output high-level semantic state JSON (screen name, modal state, cursor field/value, list rows). |
| `--serve` | — | Interactive daemon mode reading line-delimited commands from `stdin` and emitting JSON responses to `stdout`. **This is the mode to use for anything multi-step** — see the verb table below and [`m8drv`](m8drv.md), the supervised client for it. |
| `--allow-mutation` | `false` | Required flag for `--pin-gestures` to permit sending edit commands to the device. |
| `--find-file <name>` | — | Navigate to LOAD PROJECT modal and recursively search file tree up to 4 levels / 64 directories for `<name>`. Returns candidates in envelope JSON. |
| `--load-song <name>` | — | Search for `<name>` using tree search and select/load it via closed-loop navigation. |
| `--ui-capture <path>` | — | Wait for display to settle, capture full UI state (`UiCapture` struct: screen, cells, rects, palette) and write JSON file. |
| `--keyjazz <note>` | — | Send live MIDI keyjazz note-on to connected device (`0`..`127`, e.g. `60` for C-4). |
| `--keyjazz-vel <vel>` | `127` | Velocity for `--keyjazz` note (`0`..`255`, default `0x7F`). |
| `--json <path>` | — | Write the decoded grid as JSON (cells with colors and `highlights` rect array). |
| `--keys <list>` | — | Comma-separated key masks (decimal or `0x` hex) to press one at a time, dumping the screen after each. Manual/diagnostic mode. |
| `--load-file <name>` | — | Closed-loop load: navigate to PROJECT → LOAD PROJECT browser → find and select `<name>` → confirm. Returns exit code 0 on success, non-zero on failure. |
| `--goto-screen <name>` | — | Navigate to a named screen (`SONG`, `CHAIN`, `PHRASE`, `INSTRUMENT`, `TABLE`, `PROJECT`, `GROOVE`, `MODS`, `SCALE`, `INST_POOL`, `MIXER`, `EFFECTS`, or a partial match). Prints the resulting screen. |
| `--read-field <name>` | — | Move cursor to a named field and print its current value. |
| `--record-frames <path>` | — | Record decoded cell frames to a simple binary format for `--record-duration` ms (see Gotchas — not a raw SLIP recording). |
| `--record-duration <ms>` | `5000` | Duration for `--record-frames`. |
| `--pin-gestures <field>` | — | **Tier 2 gesture discovery.** Requires `--allow-mutation`. Navigates to the field, tests 17 candidate key-mask combinations against it, reports which ones edited the value vs. moved the cursor vs. did nothing. |
| `--script <path>` | — | Run a `.m8script` file against the real device via `DeviceScriptRunner`. |
| `--hold-ms <n>` | `40` | Button hold duration per press. Automatically clamped to `15` if `--load-file` is also set and this would be `> 20`. |
| `--gap-ms <n>` | `120` | Delay between a `--keys` press and reading the resulting screen. |
| `--no-reset` | `false` | Skip the `'R'` reset-request on open (`openNoReset` instead of `open`). **Without a prior full-framebuffer resend, a fresh process's decoded grid can start empty or badly incomplete** — see Gotchas. |
| `--max-ms <n>` | `2000` | Max wait ceiling for a settled screen read (`readSettled`). |
| `--settle-ms <n>` | `250` | Quiet window settle time for a screen read (`readSettled`). |
| `--min-ms <n>` | `700` | Minimum wait before the first read after opening (`readSettled`). |

If no mode flag is given at all, `--dump-screen` is implied. Unknown flags print `unknown arg:
<flag>` and exit with `ExitCode::UNKNOWN_ARG` (2). No `--port` prints full usage and exits with `ExitCode::UNKNOWN_ARG` (2).

## Screen names (for `--goto-screen`, and `.m8script`'s `goto` verb)

`SONG`, `CHAIN`, `PHRASE`, `INSTRUMENT` (or `INST`/`INST.`), `TABLE`, `PROJECT`, `GROOVE`, `MODS`
(or `MOD`), `SCALE`, `INST_POOL` (or `INSTPOOL`), `MIXER`, `EFFECTS`. Partial/substring matching
is attempted if the exact name doesn't resolve.

## `--pin-gestures`: how the edit gesture masks were discovered

Navigates to the named field's screen, moves the cursor to it, then tests 17 candidate key-mask
combinations (`SHIFT+UP/DOWN`, `EDIT+UP/DOWN/LEFT/RIGHT`, `OPT+UP/DOWN`, various 3-key combos,
plus the plain arrows for comparison) one at a time — before/after each, it reads the cursor's
label text and classifies the result as **EDITED** (same field, different value), **MOVED**
(cursor left the field entirely), or **same** (no visible effect). Re-navigates to the field post-test
and warns if the field value was mutated. Requires `--allow-mutation`. The confirmed masks get written into
[`hw_buttons.json`](../../hw_buttons.json) by hand (this mode reports candidates, it doesn't
write the file itself) — see [`Gestures.h`](../../src/tools/m8/Gestures.h) for how that file is
then loaded and used by `editValue`/`enterNote`/`clearCell`.

## `--serve` daemon verbs

Line-delimited `VERB key=value ...` on stdin; one JSON object per reply, each
carrying the full semantic state. Every verb accepts `hold=<ms>` to override
`--hold-ms`.

| Verb | Params | Notes |
|---|---|---|
| `PRESS` | `key=` | Name (`SHIFT+RIGHT`) or mask (`0x14`). `key=0` is a bare release. |
| `GOTO` | `screen=` | Any name from the list above. |
| `CURSOR` | `field=` | **Form screens only** — grid screens have no field maps. |
| `MOVEGRID` | `step=` `col=` | Grid screens, both 0-based. `step` 0-15. |
| `READ` | `field=` | Replies with **both** `value` (label stripped) and `row` (the whole row, which is `readField`'s own contract, relied on by `assertField`). |
| `SET` | `field=` `value=` | `editValue`. Values are **hex**. Needs pinned gestures. |
| `NOTE` | `name=` `vel=` | `enterNote`. Needs pinned gestures. |
| `KEYJAZZ` | `note=` `vel=` | Live note, 0-127. Does not need gestures. |
| `HOME` | `confirm=` `maxup=` | `panicHome`: clear a modal, bounded run of plain UP presses, re-check. Cancels modals unless `confirm=1`. |
| `LOAD` | `path=` | Closed-loop project load by filename. |
| `SCRIPT` | `path=` | Runs a `.m8script`. |
| `CAPTURE` | `path=` | Writes a `UiCapture` JSON. Requires a settled display. |
| `STATE` / `FIELDS` | `screen=` (FIELDS) | Refresh state / list a screen's field names. |
| `QUIT` | — | Exit. |

The semantic state carries `screen`, `is_modal`, `is_live_mode`, `settled`,
`cursor_field`, `cursor_value`, `cursor_row`, `cursor_col`, `grid_step`,
`grid_col`, `grid_columns` and `rows`. The grid triple is `-1` on form screens;
`cursor_col` is the pixel X of the cursor's leading cell.

## Primitives added 2026-08-14

- `panicHome` — the unattended recovery routine (`HOME`).
- `gridCursorPosition` / `gridColumnEdges` — grid-screen `(step, col)`, read off the
  column-header row. Single source of truth: `moveCursorToGrid` navigates by it and
  `semanticState` reports it, so acting and reporting cannot disagree.
- `cursorValueText` — the cursor's value with the field's label stripped, matched
  whitespace-insensitively because `cursorMainText()` returns only accent-coloured
  cells and which cells are accented varies between frames.

## `.m8script` verbs this driver supports (via `--script`)

`goto`, `cursor`, `set`, `note`, `key`, `hold`, `wait`, `load`, `play`, `stop`, `dump_screen`,
`dump_json`, `assert_screen`, `assert_field`, `assert_row_matches`, `assert_playing`. Verbs are
case-insensitive (`DeviceScriptRunner` upper-cases before matching). This is a **separate parser**
from the clone's own offline `ScriptRunner` (`src/ui/ScriptRunner.cpp`) — not shared code — but
deliberately uses the same lowercase text dialect, so a script written for one can run
unmodified against the other wherever their verb sets overlap. See
[`m8_diffcheck`](m8_diffcheck.md) for the tool that exploits this to diff device output against a
clone-generated golden reference.

## Exit codes (`ExitCode` enum in `Result.h`)

Exit codes are append-only and stable:

| Value | Enum Name | Meaning |
|---|---|---|
| 0 | `SUCCESS` | Command completed successfully |
| 1 | `DEVICE_NOT_FOUND` | Serial port open failure (e.g. invalid COM port) |
| 2 | `UNKNOWN_ARG` | Bad CLI flag, missing argument, or `--settle-ms >= --max-ms` |
| 3 | `UNSETTLED_DISPLAY` | Display unsettled during capture |
| 4 | `COMMAND_FAILED` | Command/script failed during execution |
| 5 | `TIMED_OUT` | Timed out waiting for device response |
| 6 | `AMBIGUOUS_MATCH` | Screen match was ambiguous or `--find-file` returned multiple matches |
| 7 | `TARGET_UNREACHABLE` | Target screen or field could not be reached |
| 8 | `NOT_FOUND` | Genuine no-match: file not found on SD card or field absent |
| 9 | `NO_DATA` | Serial port opened but no display frames decoded (device powered off) |

## Output Envelope & Payload (`M8NAV_RESULT`)

On exit, `m8_nav` emits a single structured JSON result line:

```json
M8NAV_RESULT {"ok":true,"code":0,"action":"find-file","screen":"LOADPROJECT","cursor_field":"PROBE","cursor_text":"PROBE.M8S","settled":true,"read_ms":475,"matches":["PROBE.M8S"],"dirs_visited":1,"truncated":false}
```

The envelope carries standard fields (`ok`, `code`, `action`, `message`, `screen`, `cursor_field`, `cursor_text`, `settled`, `read_ms`) plus free-form payload entries in `extra`:
- `matches`: string array of matched file paths on `AMBIGUOUS_MATCH` or `--find-file`.
- `dirs_visited`: number of directories traversed during search.
- `truncated`: boolean indicating whether search depth/visit cap was hit.

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
- **`--goto-screen` multi-hop reliability: RESOLVED as of 2026-08-14.** This previously read
  "has shown real reliability issues (landing on the wrong screen after 2+ hops)".
  `tests/hw/goto_project_test.m8script` — `goto PROJECT` from each of 12 non-modal screens, with
  `assert_screen` after every hop — now passes with zero flakes, including multi-hop routes such
  as EFFECTSETTINGS → MIXER → SONG → PROJECT. Run that script if you suspect a regression.
- **Modals often need more than one press to dismiss.** "LOSE CHANGES TO CURRENT SONG?" and
  "LOSE CHANGES TO INSTRUMENT?" have both been observed needing 2 EDIT presses where 1 appeared
  to have zero effect on the next read. A `dismissModal()` primitive (retry-until-gone) was added
  to `Primitives.cpp` specifically for this — if you're calling into `Primitives` directly rather
  than going through `--script`, use it rather than a single bare `press(EDIT)`.
- **`--load-file` works as of 2026-08-14**, where this previously recorded it "failed outright
  in testing (`rc=11`)". Loading probes by filename via the `--serve` `LOAD` verb succeeded
  repeatedly across a measurement session. Bugs #15 and #16 were the causes: `loadFile` used an
  ad-hoc SHIFT+UP climb instead of the hardened `gotoScreen`, and could only scroll DOWN toward a
  target. Both fixed.
- **`--record-frames` does not record a true SLIP stream.** Despite the name, it dumps the
  *already-decoded* cell grid (position/char/colors) in a simple binary format at a fixed polling
  interval, not the raw serial bytes — it can't be replayed through the actual SLIP decoder, only
  through custom code that understands its own ad-hoc binary layout.
- **COM port is exclusive.** Only one process can hold it — if you're also running a display
  client (or another `m8_nav`/`m8_capture` process), disconnect it first.
