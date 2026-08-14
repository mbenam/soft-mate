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

`FENCED_FIELDS` — the MIXER compound widget (`MST_CHO`, `MST_DEL`, `MST_REV`,
`MIX_VOL`, `LIM_VAL`, `DJF_FREQ`, `DJF_RES`, `DJF_TYP`). Bug #20 is OPEN and is
*device behaviour*: navigation there is not a pure function of (field, key), proven
by an isolated hop-by-hop path giving a different result on identical re-test. No
driver in any language over any transport fixes that with a fixed key sequence, so
these fail immediately instead of thrashing for 30 s.

`HOME_FIRST_FIELDS` — `TYPE`. Bug #21 only bites from a dirty cursor position
(its row carries an unmapped LOAD/SAVE pair), so this one gets a forced known
position rather than a refusal.

## Commands

```
doctor                 prove the loop end-to-end; reports firmware + gestures_ready
dump | state           decoded screen / raw semantic JSON
goto <SCREEN>          SONG CHAIN PHRASE INSTRUMENT TABLE PROJECT GROOVE MODS
                       SCALE INST_POOL MIXER EFFECTS
cursor <FIELD>         move the cursor to a named field
read <FIELD>           read a field's value
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
