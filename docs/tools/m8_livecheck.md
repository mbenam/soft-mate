# m8_livecheck

**Source:** [`src/tools/main_livecheck.cpp`](../../src/tools/main_livecheck.cpp)
**Build target:** `m8_livecheck` (CMakeLists.txt, links `m8_device`)
**Category:** hardware diagnostic for the live read model. Proves on the device what
[`tests/test_live_reader.cpp`](../../tests/test_live_reader.cpp) proves against a fake — that a
settle-gated read cannot complete while the transport runs, and that
[`LiveReader`](../../src/tools/m8/LiveReader.h) can.
**Links:** `m8_device` only. No engine, no SDL, no audio. Serial only.

## What it does

Opens the port, reads a baseline, starts the transport if it is not already running, and then
measures the same window two ways: one settle-gated `readSettled` through the pull path, and a
few seconds of `LiveReader` snapshots. It prints one JSON object and restores the transport to
the state it found it in.

It presses exactly one key — `PLAY` — and only after reading whether the transport is already
running, because `PLAY` is a toggle (`M8_DRIVER_BUGS.md` #28). It is a diagnostic, not a driver:
it never navigates, so it measures whatever screen the device was left on. That matters — see
below.

## CLI flags

| Flag | Required | Meaning |
|---|---|---|
| `--port <name>` | no (`COM3`) | Serial port, e.g. `COM3`. |
| `--seconds <n>` | no (`4`) | Length of the live sampling window. |
| `--help` | no | Print usage and exit 0. |

## Exit codes

| Code | Meaning |
|---|---|
| 0 | The live reader stayed current and observed the screen change during a window in which the pull read timed out. |
| 1 | It did not. The JSON says which half failed — `live_kept_up` or `saw_screen_change`. |
| 2 | Could not open the port, or `LiveReader::start()` refused it. |

## Run it on a screen where something moves

The playhead marker names the **step** on PHRASE and the **song row** on SONG, and a song row
holds for a whole chain — minutes at a normal tempo. Four seconds on SONG therefore reports one
screen change and one playhead row, which is the truth about SONG rather than a fault in the
reader. Navigate first:

```powershell
python tools/m8drv/m8drv.py goto PHRASE
build\Release\m8_livecheck.exe --port COM3 --seconds 4
```

m8drv's daemon must have exited before this runs — COM3 is exclusive.

## Measured, fw 6.5.2, 2026-08-24

On PHRASE, transport running, a 4-second window:

| | Pull path (`readSettled 0/250/2000`) | `LiveReader` |
|---|---|---|
| Completed a read | **no** — `timed_out`, `settled: false` | 128 snapshots |
| Time spent | 2017 ms, nothing to show for it | 208 µs mean per snapshot |
| Frames decoded | — | 2173 (543/s) |
| Longest gap without data | — | 34 ms |
| Playhead rows resolved | 0 | **16** — every step of the phrase |
| Screen changes seen | 0 | 37 |

The pull path is not slow here, it is blind: the M8 redraws continuously while playing, so
`sinceData` never reaches `settleMs` and every read runs the full `maxMs` and returns
`timedOut`. That is `readInto` behaving correctly. It is also why watching a field across an
audio capture — the guard [`hw_measure.py`](hw_measure.md) has to approximate by re-reading
before and after — needs the live model.

The same run on SONG, for contrast: `distinct_screens: 2`, `distinct_playhead_rows: 1`,
`frames_per_second: 397`. Hundreds of frames a second carrying an unchanged picture. The traffic
is not the motion, which is exactly why the pass criterion digests the decoded grid rather than
counting frames.

## Fields

| Field | Meaning |
|---|---|
| `baseline_read_settled` / `baseline_read_ms` | The first read, before anything is pressed. Expect `false` and ~2000 ms: `open()` sends `'R'`, a full framebuffer resend, and ~15 KB at 115200 baud does not finish and go quiet inside `maxMs`. Benign, but reported rather than hidden. |
| `transport_was_running_on_entry` | Whether `PLAY` was pressed at all, and whether it is pressed again to restore. |
| `pull_read_during_playback` | `settled` / `timed_out` / `quiet_ms_at_exit` / `elapsed_ms` straight off `ReadStats`. |
| `live_read_during_playback` | `snapshots`, `frames_decoded`, `frames_per_second`, `max_quiet_ms`, `distinct_screens`, `screen_changes_observed`, `distinct_playhead_rows`, `mean_snapshot_us`. |
| `live_kept_up` | Frames advanced across the window. |
| `saw_screen_change` | The decoded grid changed at least once — glyphs, colours or rects. |
| `saw_playhead_move` | The marker occupied more than one row. Informational: false on SONG is expected. |

## Gotchas

- **It measures the screen it finds.** No `goto`. See above.
- **`saw_playhead_move` is not the pass criterion**, and asserting it was a bug in the first
  version of this tool — it failed a working reader because SONG's marker had not moved yet.
- **The digest folds cells *and* rects.** An earlier version digested glyph plus foreground only
  and reported one change across 1595 frames, which read as "streams constantly, draws nothing".
  Most of what moves during playback is rect fills and background colour.
- **COM3 is exclusive.** `m8_nav`, `m8drv`, `m8_capture` and any display client must be closed.
- **While a `LiveReader` is running, pull reads on that device refuse** rather than race it —
  `ReadStats::liveConflict`. Two drains feeding one `SlipDecoder` desync it, and a desynced
  stream decodes as a plausible screen with drifting rows (#32).

## Example

```powershell
build\Release\m8_livecheck.exe --port COM3 --seconds 4
```
