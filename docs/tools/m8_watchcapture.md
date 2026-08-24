# m8_watchcapture

**Source:** [`src/tools/main_watchcapture.cpp`](../../src/tools/main_watchcapture.cpp)
**Build target:** `m8_watchcapture` (CMakeLists.txt, links `m8_device` **and** `m8_audiocap`)
**Category:** guarded hardware measurement — records the M8's USB audio while decoding its
screen, and aborts the moment the capture stops being the one you asked for.
**Links:** `m8_device` for the display, `m8_audiocap` for the audio. No engine, no SDL.

## What it does

Holds the serial port, the audio device and the guard in one process. Takes a settled read to
establish baselines, starts a [`LiveReader`](../../src/tools/m8/LiveReader.h), starts recording,
and then samples the screen every 10 ms for the whole capture window — failing fast instead of
producing a number that has to be thrown away later.

It is the "during" that [`hw_measure.py`](hw_measure.md) could not have. That harness's rule is
*"re-read every field its result depends on, immediately before AND after the capture"* —
before-and-after is what you do when you cannot look during, and until `LiveReader` existed
nothing could:

- `m8_capture` speaks serial but does not decode the display. Its own keyjazz warning says so.
- `m8_nav` decodes the display but [cannot read during playback at all](m8_livecheck.md) — the
  settle window never arrives, so every read returns `timedOut`.
- COM3 is exclusive, so the two cannot be run side by side.

## The two failures it is built to catch

| | What happened | What catches it now |
|---|---|---|
| **#34** (2026-08-19) | `set AMP FF` silently moved LIM from 04 to 08. The sweep point taken at the wrong LIM looked like a real measurement and survived until a later read noticed. | `--watch AMP --watch LIM` — the guarded rows are re-read every 10 ms. |
| 2026-08-20 | `PLAY` is a toggle. The start press stopped an already-playing device and the stop press started it, so the window held the silence between notes. It latched after three good captures and poisoned the remaining **seventeen**. | The transport is established before `PLAY` is pressed — by the playhead where it is drawn, by listening for quiet where it is not. See the transport probe below. |

## CLI flags

| Flag | Required | Meaning |
|---|---|---|
| `--port <name>` | no (`COM3`) | Serial port. |
| `--audio <substr>` | no (`M8`) | Capture device whose name contains this. Available devices are listed on failure. |
| `--seconds <n>` | no (`3`) | Capture length. |
| `--out <path.wav>` | no (`watchcapture.wav`) | Output WAV. A `.manifest.json` and a `.timeline.json` are written beside it. |
| `--watch <LABEL>` | no, repeatable | Guard the row this label sits on. Whole row, not the parsed value. |
| `--sample-ms <n>` | no (`10`) | Screen sampling interval. |
| `--pre-roll <ms>` | no (`5`) | Silence kept before the detected onset. |
| `--silence-floor <f>` | no (`0.002`) | Level below which the device counts as quiet. |
| `--probe-ms <n>` | no (`8000`) | Longest the transport probe waits for the device to go quiet. |
| `--help` | no | Print usage and exit 0. |

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Clean capture. |
| 1 | A guard fired. **The WAV is still written**, with `"guard_passed": false` in the manifest — a rejected capture is evidence about the device, not nothing. |
| 2 | Could not open the port, the audio device, or the live reader; or a `--watch` label is not on the current screen. |

## Output

Three files. The WAV is 48 kHz 16-bit stereo, onset-trimmed, identical in format to
[`m8_capture`](m8_capture.md)'s — they share one writer. The manifest carries a `watch` block
(guard verdict, abort reason, screen, transport state on entry, per-field `held`). The timeline is
one row per screen sample: `t_ms`, `seq`, `playing`, `quiet_ms` — the audio and the screen on one
clock, which is the artifact `m8_capture` cannot produce.

## What the guard will and will not fire on

Pinned in [`tests/test_field_guard.cpp`](../../tests/test_field_guard.cpp), because a guard that
has never been seen to fire is indistinguishable from one that cannot — and its first live run
passed, which proves the plumbing and says nothing about the decision.

| Situation | Drift? | Why |
|---|---|---|
| `LIM 04` → `LIM 08` | **yes** | The #34 case. |
| `" OUTPUT VOL  F0"` → `"OUTPUTVOLF0"` | no | Which cells carry the theme accent varies frame to frame. Comparison is alnum-only. |
| Playhead `>` enters the row | no | `canonRow` drops non-alnum, so the transport running through a guarded row is not a change. |
| Row reads empty | no | The live grid is sampled without waiting for quiet, so a row can be caught between erase and redraw. |

Comparison is on the **whole row**, not the parsed value: conflating those was already a bug once
(m8drv's `read` vs `read --row`), and #34 moved a *neighbouring* field, which a value-scoped
check would have missed.

## The transport probe

`PLAY` is a toggle, so pressing it blind on an already-playing device stops it — the 2026-08-20
latch. Where the playhead is drawn, it answers this. Where it is not, the tool **listens before
pressing**: a device already playing is already making sound.

A single listen is not enough, and hardware said so on the first try. With the transport confirmed
stopped two seconds earlier, one 400 ms sample still read `0.385` and concluded "playing". **A
stopped M8 rings** — measured decay `0.354 → 0.0034` over five seconds, monotonic. An
instantaneous level cannot separate a tail from a song.

So it waits for quiet instead: the device must stay below `--silence-floor` for 400 ms
uninterrupted. A tail gets there; a running song does not. If nothing goes quiet inside
`--probe-ms`, it calls the device playing and does **not** press — the safe direction, since
leaving a playing device playing yields a valid capture and pressing PLAY on one yields the
poisoned kind.

Verified on hardware, fw 6.5.2, 2026-08-24, parked on INSTRUMENT where the playhead is invisible:

| Entry state | Probe | Decision | Result |
|---|---|---|---|
| Stopped | heard the tail at `0.291`, then quiet | pressed PLAY | capture peak `0.312` |
| Playing | never went quiet, peak `1.013` | did not press | capture peak `1.039`, transport left running |

`probe_peak`, `probe_went_quiet` and `probe_quiet_after_ms` go into the manifest, so a wrong call
is visible after the fact rather than silent.

## Gotchas

- **On a form screen the playhead is invisible, and the audio covers it.** PROJECT, INSTRUMENT,
  MIXER and SCALE draw no playhead (`M8_DRIVER_BUGS.md` #28), so `playhead_observable` is false
  and the screen cannot say whether the transport is running. Since this tool holds the audio
  too, it listens instead — see the transport probe below. This is the common case for
  measurement, since hw_measure parks on INSTRUMENT to keep keyjazz out of a phrase.
- **The transport guard ignores the first 400 ms.** The marker takes a moment to appear after the
  press; firing on that would reject every capture.
- **`--watch` labels must be on the screen at start**, or it exits 2 rather than guarding nothing.
- **COM3 is exclusive.** Park the device with m8drv first, and let its daemon exit.
- **It presses `PLAY` at most twice** — once to start if the device was not already playing, once
  to restore. Never blind: the transport is read, or listened for, first.
- **A deliberately silent probe will be misread as a playing device**, because "never goes quiet"
  and "never makes a sound" are distinguished by the end-of-capture no-signal check, not by the
  probe. Capturing a noise floor is the one case to pass `--probe-ms 0` for.

## Example

```powershell
python tools/m8drv/m8drv.py goto INSTRUMENT
build\Release\m8_watchcapture.exe --port COM3 --audio M8 --seconds 3 --out probe.wav --watch AMP --watch LIM
```

Measured on fw 6.5.2, 2026-08-24: 181 screen samples across a 3.0 s capture, guard passed,
peak 0.385.
