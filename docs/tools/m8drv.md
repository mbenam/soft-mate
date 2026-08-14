# m8drv

**Source:** [`tools/m8drv/m8drv.py`](../../tools/m8drv/m8drv.py) (Python 3, stdlib only),
plus [`tools/m8drv/README.md`](../../tools/m8drv/README.md) and the parked
[`shim.js`](../../tools/m8drv/shim.js).
**Build target:** none — it is a Python client. It *drives* `m8_nav --serve`, so
`m8_nav` must be built.
**Category:** supervision layer over the real-hardware device driver. Not a second
driver: it adds process lifecycle, timeouts and automatic recovery around the
existing `m8_device` primitives.
**Links:** nothing. Spawns `m8_nav.exe` as a subprocess.

## What it does

Holds **one** `m8_nav --serve` process open and sends it many commands, instead of
running a separate `m8_nav --flag` process per command.

That distinction is the whole point. `M8Device::open`/`close`
([M8Device.cpp:491](../../src/tools/m8/M8Device.cpp:491)) do:

```
open():  'E' → sleep 500ms → 'R'    enable display, then full display reset
close(): 'D'                        disconnect
```

So N one-shot CLI calls are N connect / display-reset / disconnect cycles. Every
read lands on a freshly reset framebuffer, per-connection device state dies
between presses, and each command costs a fixed 500 ms. The symptom is a device
that looks like it has stopped accepting keys when nothing is wrong with it.

It also matters for anything needing more than one press to converge. The
`LOSE CHANGES TO INSTRUMENT?` modal needs **two** EDIT presses, the first
appearing to do nothing
([M8_DEVICE_CONTROL_SPEC.md §6.5.3](../../specs/M8_DEVICE_CONTROL_SPEC.md:514)), and
[`dismissModal`](../../src/tools/m8/Primitives.cpp:108) retries for exactly that
reason. One press per process can never reach two.

## Why not just use `m8_nav --serve` directly

The daemon has no timeouts and no recovery. When a C++ primitive spins, the daemon
stops reading stdin, so it cannot be talked down — it has to be killed. This client
does that, and knows what to do afterwards.

## Recovery model

| Trigger | Action |
|---|---|
| Command exceeds its timeout | Kill the daemon (releases COM3), restart it |
| After every restart | Send `'C' 0x00` — a bare key release, in case the kill landed between mask-down and mask-up and left a key held auto-repeating at ~150 ms |
| Then | `HOME` → [`panicHome`](../../src/tools/m8/Primitives.cpp) |

`panicHome` clears a modal, then runs a **bounded run of plain UP presses** —
per [bug #20](../../specs/M8_DRIVER_BUGS.md) the only escape from a stuck widget that
hardware testing found reliable, and specifically *not* a bounce to another screen
and back, because the M8 remembers its per-screen cursor position.

It **cancels** modals (OPT) and refuses to press EDIT blind, since cancelling can
never commit an edit while confirming can. If a modal will not cancel it fails with
the modal still in the snapshot. `HOME confirm=1` opts in.

`send()` raises after recovering and never silently retries — a command that timed
out may already have taken effect on the device, so repeating it is the caller's
decision.

## CLI

| Command | Meaning |
|---|---|
| `doctor` | End-to-end health check. Reports firmware, `gestures_ready`, screen drift, and a cursor key round-trip. Run this first. |
| `dump` / `state` | Decoded screen / raw semantic-state JSON |
| `probe <KEY> [--times N]` | Press a key repeatedly and report exactly what moved. The diagnostic for "did the press land, or is cursor tracking wrong?" |
| `goto <SCREEN>` | `SONG CHAIN PHRASE INSTRUMENT TABLE PROJECT GROOVE MODS SCALE INST_POOL MIXER EFFECTS` |
| `cursor <FIELD>` | Move to a named field — **form screens only** |
| `cursor-grid <STEP> <COL>` | Move on a grid screen — see Gotchas |
| `read <FIELD>` | Read a field's value |
| `set <FIELD> <VALUE>` | Edit a field (needs pinned gestures) |
| `press <KEY>` | `UP`, `SHIFT+RIGHT`, or a raw `0x14` |
| `note <NAME>` / `keyjazz <0-127>` | Enter a note / send a live note |
| `load <NAME>` | Closed-loop project load |
| `home [--confirm]` | Run the recovery routine by hand |
| `fields [SCREEN]` | List drivable field names |
| `capture <PATH>` / `script <PATH>` | `UiCapture` JSON / run a `.m8script` |
| `repl` | Interactive session |

Global: `--port` (default `COM3`), `--exe`, `--hold-ms` (default 40), `-v`,
`--no-recover`.

`press` takes key **names** on purpose. `RIGHT` (`0x04`) moves the cursor within a
screen; `SHIFT+RIGHT` (`0x14`) moves between screens. Hand-computing that mask
wrong produces plausible-but-wrong behaviour rather than an error.

## Gotchas

- **The daemon always runs with the repo root as its working directory.**
  `m8_nav` loads edit gestures from the bare relative path `hw_buttons.json`
  ([main_nav.cpp:436](../../src/tools/main_nav.cpp:436)) and has no flag to override
  it. Without them `editValue` refuses every `SET`. `doctor` reports
  `gestures_ready` so this is visible rather than mysterious.
- **Form screens and grid screens are addressed differently.** SONG, CHAIN,
  PHRASE, GROOVE, TABLE and INST_POOL are grid screens: `getFieldMap().isGrid` is
  true and every field lookup returns `nullopt`
  ([ScreenModel.h:564](../../src/tools/m8/ScreenModel.h:564)), so `cursor <FIELD>`
  cannot reach them at all. Use `cursor-grid <step> <col>`.
- **`cursor_row`/`cursor_field` are wrong on grid screens, and `cursor-grid` fails
  because of it** — bug #22 in [`M8_DRIVER_BUGS.md`](../../specs/M8_DRIVER_BUGS.md).
  On SONG the reported cursor row is y=50, which is the *track-number header*, not
  a data row (data starts at y=60). Confirmed working on PROJECT (cursor tracked
  50→60→70→80 through TRANSPOSE/GROOVE/SCALE, `baseline_drift: 0`), confirmed
  broken on SONG. Use `rects --key DOWN` to see whether the grid cursor is a rect
  fill — if it is, `cursorRowY()` cannot see it by construction, since it skips
  highlighted cells.
- **Rect fills are invisible to the semantic state.** `state`/`dump` show only
  cells; the `highlights` array is not in `SemanticState` at all. Use `rects`.
- **Never compare whole-screen snapshots to detect a change.** `settled` flaps
  between reads, so any comparison including it differs whether or not anything
  moved. This produced a false "keys reach the device" in the first `doctor`
  version. Compare the cursor, or compare row text — not the blob.
- **A cursor move changes colour, not text.** `probe`'s `rows_changed` is 0 for a
  pure cursor move, which is expected and not a failure.
- **The M8 does not auto-home.** It keeps whatever screen it was left on, so a
  `probe` with no preceding `goto` runs against the previous session's screen.
- **COM3 is exclusive.** While this holds the port, `m8_nav`, `m8_capture` and any
  display client cannot open it. The daemon is killed on exit (`QUIT`, or a kill
  on timeout), which releases it.
- **PowerShell 5.1 has no `&&`.** Chain with `;` or use separate lines.

## Also in that directory

`shim.js` is **not used**. It taps the display protocol inside the m8.run web
client — wraps `navigator.serial`, tees the read stream, decodes SLIP in-page to a
text+colour grid, and can send `'C'`/`'K'` control bytes. m8.run auto-reconnects
via `navigator.serial.getPorts()` filtered on VID `0x16C0` / PID `0x048A`–`0x048B`,
so after one manual permission grant it needs no dialogs.

Parked deliberately: routing automation through a browser would add Chrome, a
granted profile and a live CDP attach to the list of things that must be true
before an agent can start, and it competes for COM3. It is kept because it is a
correct, *independent* second reader — useful for cross-checking the C++ decoder,
or for letting a human watch and take over.

## Example

```powershell
python tools/m8drv/m8drv.py doctor
python tools/m8drv/m8drv.py goto PROJECT
python tools/m8drv/m8drv.py probe DOWN --times 3
python tools/m8drv/m8drv.py read TEMPO
```
