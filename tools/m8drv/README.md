# m8drv — unattended driver for a real M8 headless

A supervised Python client for `m8_nav --serve`. It exists so an agent can drive
the device with **no human hand on it** and no screen to look at.

```bash
python tools/m8drv/m8drv.py doctor
```

## Why this rather than one-shot `m8_nav` calls

`M8Device::open`/`close` ([M8Device.cpp:491](../../src/tools/m8/M8Device.cpp:491)) do:

```
open():  'E' → sleep 500ms → 'R'   enable display, then full display reset
close(): 'D'                       disconnect
```

Every one-shot CLI invocation is its own process, so a sequence of N commands is
**N connect / display-reset / disconnect cycles**. Each read lands on a freshly
reset framebuffer, per-connection device state dies between presses, and each
command costs a fixed 500 ms. That presents exactly as "the device stopped
accepting keys" when nothing is wrong with the device.

This is not hypothetical. On 2026-08-14 a session concluded the device was wedged
on a `LOSE CHANGES TO INSTRUMENT?` modal because EDIT, OPT and longer holds all
"did nothing". Two facts in the repo contradict that reading:

- [M8_DEVICE_CONTROL_SPEC.md §6.5.3](../../specs/M8_DEVICE_CONTROL_SPEC.md:514) records
  that this exact modal needs **two** EDIT presses, the first appearing to do
  nothing — reproduced live, multiple times.
- [`dismissModal`](../../src/tools/m8/Primitives.cpp:108) was built for precisely that,
  with bounded retries, and [`isModal`](../../src/tools/m8/ScreenModel.h:131) already
  matches `"LOSE CHANGES"`.

One press per process can never accumulate to two presses. The modal was almost
certainly dismissable the whole time; the retry loop just never ran.

## Setup

```bash
cmake --build build --config Release --target m8_nav
```

Nothing else. No browser, no display, no permissions.

The daemon is always spawned with the **repo root** as its working directory,
because `m8_nav` loads its edit gestures from the bare relative path
`hw_buttons.json` ([main_nav.cpp:436](../../src/tools/main_nav.cpp:436)) and has no
flag to override it. Without them, `editValue` refuses every `SET`. `doctor`
reports `gestures_ready` so this is visible rather than mysterious.

## What makes it unattended

1. Every command has a timeout (see `TIMEOUTS`; generous, because `editValue` can
   step a byte up to 256 times and `LOAD` searches the SD card tree).
2. On timeout the daemon is **killed and restarted**. This is necessary, not
   lazy: a daemon spinning inside a C++ primitive is not reading stdin, so no
   command can reach it. The kill also releases COM3.
3. After restart, a key **release** (`'C' 0x00`) is sent first, in case the kill
   landed between mask-down and mask-up and left a key held auto-repeating.
4. Then `HOME` → `panicHome`: clear any modal, then a bounded run of plain UP
   presses (per [bug #20](../../specs/M8_DRIVER_BUGS.md) the only escape from a stuck
   widget that hardware testing found reliable), then re-check for a modal.

`panicHome` **cancels** modals (OPT) and will not press EDIT blind, because
cancelling can never commit an edit while confirming can. If a modal refuses to
cancel it fails with the modal still in the snapshot and lets the caller decide.
Pass `--confirm` / `confirm=1` to opt in.

## What it refuses, on purpose

`FENCED_FIELDS` — narrowed from eight fields to four on 2026-08-14 after re-testing
with working position reads. What remains is the MIXER `MX`/`DE`/`RE` column
(`MST_CHO`, `MST_DEL`, `MST_REV`) plus `DJF_TYP`, which was never located on-device.

Bug #20 originally recorded this as unfixable device behaviour, on the evidence that
an isolated hop-by-hop path gave a different result on identical re-test. **That did
not reproduce.** Two runs from a homed start gave byte-identical cursor sequences,
and `cursor MIX_VOL` now simply works — the original evidence rested on hop-by-hop
position reads, and three defects in exactly that were fixed the same day (#22/#23/
#24). The real reason the remaining three fail is that MIXER's cursor order is a
linear chain, not a 2D grid: `probe RIGHT` walks rows 50 → 120 → 160 → 170 → 180 →
190, so RIGHT moves *down*, while `moveCursorTo` drives screens as a grid. That chain
never visits the column those three live in.

They stay fenced because a fixed key sequence burns 40 presses getting nowhere, not
out of caution. `--unfence` attempts them anyway.

`HOME_FIRST_FIELDS` — `TYPE`. Bug #21 only bites from a dirty cursor position
(its row carries an unmapped LOAD/SAVE pair), so this one gets a forced known
position rather than a refusal.

## Grid screens

SONG, CHAIN, PHRASE, GROOVE, TABLE and INST_POOL have no field names at all —
`getFieldMap().isGrid` is true and every field lookup returns `nullopt`. Address
them with `cursor-grid <step> <col>`, both 0-based (track 1 on SONG is `col 0`),
and read position from `grid_step` / `grid_col`, which `dump` prints. Don't read
`cursor_field` there: on a grid screen it is the row label glued to the cell text
(`"07--"`), naming neither axis.

Five hardware-confirmed bugs stood between this working and not, all found by
driving the device and all recorded in
[`M8_DRIVER_BUGS.md`](../../specs/M8_DRIVER_BUGS.md) #22–#26. The one worth knowing
about, because it shapes how you debug this tool: **#24 was invisible to one-shot
invocations.** `open()` sends `'R'`, a full-framebuffer resend that repaints away
the stale accent-coloured blanks the M8 leaves at a vacated row, so a fresh
process always reads correctly and only the *second and later* reads within one
connection went wrong. Holding the connection open is what surfaced it.

If a position read ever looks stale again, `probe <KEY> --times 3` is the
instrument — it reports `cursor` and `grid` per press, and a cursor that moves
while the grid coordinates stand still is that same signature.

## Commands

```
doctor                 prove the loop end-to-end; reports firmware + gestures_ready
dump | state           decoded screen / raw semantic JSON
goto <SCREEN>          SONG CHAIN PHRASE INSTRUMENT TABLE PROJECT GROOVE MODS
                       SCALE INST_POOL MIXER EFFECTS
batch [FILE]           run many commands over ONE connection, from a file or
                       stdin, as "VERB k=v" lines -- use this for anything
                       multi-step; see Speed below
cursor <FIELD>         move the cursor to a named field -- form screens only
cursor-grid <ST> <COL> move the cursor on a grid screen, both 0-based
probe <KEY> [--times]  press a key N times; report cursor + grid per press
inspect [--key K]      accent cells as fg AND bg, plus rect fills; the only
                       view of colour in the toolchain
read <FIELD> [--row]   a field's value, label stripped; --row for the whole row
set <FIELD> <VALUE>    edit a field (needs pinned gestures)
press <KEY>            e.g. UP, SHIFT+RIGHT, 0x14
note <NAME> [--vel]    enter a note at the cursor
keyjazz <0-127>        live note, for audio capture
load <NAME>            closed-loop project load
home [--confirm]       run the recovery routine
fields [SCREEN]        list drivable field names
capture <PATH>         write a UiCapture JSON
script <PATH>          run a .m8script on the device
repl                   interactive session
```

## Speed

One invocation per command is the mistake this tool exists to avoid. Each process
start pays `open()`'s 500 ms `'E'`-then-`'R'` sleep plus the first read's floor, so
roughly a second of dead time before anything happens — a dozen-command shell script
spends most of its life in handshakes. `batch` runs the lot over one connection:

```powershell
"GOTO screen=INSTRUMENT`nSET field=PAN value=80`nREAD field=PAN" | python tools/m8drv/m8drv.py batch
```

It prints one line per command with the resulting field, value, row and column, and
exits non-zero if any failed. The read-timing flags (`--min-ms` / `--settle-ms` /
`--max-ms`, defaulting to 250/200/1500) are already lower than `m8_nav`'s own
700/250/2000; raise them if reads start coming back unsettled.

## Values are hex

`set PAN 80` is centre, not decimal 128. The device displays byte fields in hex and
`editValue` parses the screen as hex, so the target is parsed the same way. This was
a real bug — base-0 parsing made a bare `80` mean decimal 80 and converge silently on
`0x50` (bug #26). Decimal-display fields like `TEMPO` cannot be edited at all:
`readCursorValue` yields `"120.00"` whose leading hex run parses as `0x120`, so no
target ever matches.

`press` takes key **names**, which removes a whole class of silent error: `RIGHT`
(`0x04`) moves the cursor within a screen, `SHIFT+RIGHT` (`0x14`) moves between
screens, and hand-computing the mask wrong produces plausible-but-wrong behaviour
rather than an error.

## Library use

```python
from m8drv import M8Driver, FencedField

with M8Driver(port="COM3", verbose=True) as d:
    d.goto("INSTRUMENT")
    print(d.read_field("CUTOFF"))
    d.set_field("CUTOFF", "40")
```

`send()` raises on timeout *after* recovering, and never silently retries a
command that may already have taken effect on the device — the caller decides.

## Also here

`shim.js` is **not used by this driver**. It taps the M8 display protocol inside
the m8.run web client (wraps `navigator.serial`, tees the read stream, decodes
SLIP in-page). Parked deliberately: it is a correct, independent second reader,
useful for cross-checking the C++ decoder or for letting a human watch and take
over in the browser. It is not in the automation path, because that would add
Chrome, a granted profile and a live CDP attach to the list of things that must
be true before an agent can start — and it competes for COM3.
