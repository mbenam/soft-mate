# m8_crawl

**Source:** [`src/tools/main_crawl.cpp`](../../src/tools/main_crawl.cpp), plus the comparison in
[`src/tools/m8/CrawlCheck.h`](../../src/tools/m8/CrawlCheck.h)
**Build target:** `m8_crawl` (CMakeLists.txt, links `m8_device`)
**Category:** ground truth for the field maps — walks a screen's cursor chain exhaustively and
records every stop it actually has, plus the navigation graph.
**Links:** `m8_device` only. No engine, no SDL, no audio. Serial only.

## Why it exists

The field maps in `ScreenModel.h` are hand-typed coordinates that nothing ever checked against a
device, and `M8_DRIVER_BUGS.md` is largely a list of the consequences: **#9** (every EFFECTS row
off by one), **#11** (SCALE LOAD/SAVE columns wrong), **#17** (a PROJECT field that does not exist
on this firmware), **#21** (the TYPE row), **#31** (a cursor on LOAD reported as TYPE). Each was
found by hand, one at a time, usually after it had already caused a wrong edit.

**#20** is the clearest case. Its entry blamed hidden state and called the widget unfixable — but
the chain is perfectly deterministic, and `kMixerFields` simply points `MST_CHO/DEL/REV` at column
72, which the cursor never visits. The coordinates named the `MX DE RE` column *header* instead of
the send-return values a row above it. The fields were never unreachable; the driver was aiming at
nothing.

So stop typing coordinates and go and look.

## What it produces

From a homed cursor it presses every direction from every stop it finds, until the set closes, and
writes JSON containing:

- **every real cursor stop** — pixel and grid coordinates, the accent text seen there, and the
  shortest key path from home
- **the navigation graph** — one entry per `(from, key, to)` edge

The first is ground truth to diff the maps against. The second is what would turn `moveCursorTo`
from a pile of heuristics — axis fallback, `col <= gridCol greatest wins`, `kMaxFieldSpan`, each
with its own bug number — into a path search over measured edges.

## CLI flags

| Flag | Required | Meaning |
|---|---|---|
| `--port <name>` | no (`COM3`) | Serial port. |
| `--screen <name>` | no (`MIXER`) | Screen to crawl. |
| `--out <path.json>` | no (`crawl.json`) | Where to write the crawl. |
| `--check <path.json>` | no | **Offline.** Diff a recorded crawl against the field maps; no device needed. |
| `--hold-ms <n>` | no (`20`) | Key hold duration. |
| `--max-stops <n>` | no (`200`) | Safety bound on the search. |
| `--help` | no | Print usage and exit 0. |

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Crawled, or `--check` found no disagreement. |
| 1 | `--check` found a disagreement. |
| 2 | Setup failed — port, unknown screen, or no cursor after homing. |

## What `--check` reports, and why both directions matter

| | Meaning | The bug it is |
|---|---|---|
| `phantom_fields` | A mapped field whose coordinates match no real stop. The driver aims at a cell that does not exist. | #20, #17 |
| `unclaimed_stops` | A real stop no mapped field covers. **The more dangerous half:** `identifyCursorField` hands it to whatever mapped field sits nearest to its left and reports that with confidence. | #31, #21 |

A partial map is worse than an empty one, which is why the second direction is checked at all.

## Measured, fw 6.5.2, 2026-08-24

First crawl of MIXER: **22 stops, 82 edges**. A careful manual sweep the same day had found 15.

`--check` against `kMixerFields` came back red, and correctly:

```
phantom_fields:   MST_REV, DJF_TYP
unclaimed_stops:  15 — including all eight track volumes on row 9,
                  which kMixerFields does not model at all
```

The artifact is committed at [`tests/fixtures/crawl/mixer_fw652.json`](../../tests/fixtures/crawl/mixer_fw652.json)
and [`tests/test_crawl_check.cpp`](../../tests/test_crawl_check.cpp) runs the comparison offline in
CI. That test currently asserts the disagreement **still exists** — when the map is corrected it
will fail, and that is the intended signal to flip it to `CHECK(r.ok())`.

## Gotchas

- **It replays from home for every probe**, rather than walking back. The M8 remembers a per-screen
  cursor position, so "press UP a lot" from an unknown place is not a reset (#20). That makes a
  crawl slow — minutes, not seconds — and it is the reason the results are trustworthy.
- **It reports replay drift to stderr and skips that probe** rather than recording an edge it is
  not sure about. A wrong edge in the graph is worse than a missing one.
- **Grid screens have no field map**, so `--check` reports `checkable: false` rather than success.
  A gate that passes by knowing nothing is not a gate.
- **The crawl is per firmware and per screen state.** The instrument screen relabels and re-lays-out
  with PLAY mode (REPITCH renames DETUNE to STEPS and shifts every row below it), so an INSTRUMENT
  crawl is only valid for the mode it was taken in.
- **COM3 is exclusive.** Let any m8drv daemon exit first.

## Example

```powershell
build\Release\m8_crawl.exe --port COM3 --screen MIXER --out mixer.json
build\Release\m8_crawl.exe --check mixer.json
```
