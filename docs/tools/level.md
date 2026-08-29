# level — the master-chain level rig

**Source:** [`tools/level_run.py`](../../tools/level_run.py),
[`tools/level_measure.py`](../../tools/level_measure.py),
[`tools/step_cell.py`](../../tools/step_cell.py) (Python 3; `level_measure` needs numpy).
**Build target:** none. They drive [`m8drv`](m8drv.md) and [`m8_capture`](m8_capture.md).
**Findings:** [`hw_findings.md`](hw_findings.md) §UI-31.

Measuring a transfer curve — output level against input level — for the mixer's
`LIM` and `OTT`.

## `level_run.py` — set a field, guard it, capture

```powershell
python tools/level_run.py --screen MIXER --field TRACK1_VOL `
    --values 40 60 80 A0 C0 E0 --tag lim80 `
    --expect "MIXER:LIM 80" --expect "MIXER:OTT 00"
```

Per point: set the field and verify it, snapshot INSTRUMENT and MIXER, capture,
snapshot again, and rename the WAV `.DRIFTED` if any row moved. It holds **one**
`m8_nav --serve` session per snapshot rather than spawning four processes, which
is what makes eighty guarded captures affordable.

| Exit | Meaning |
|---|---|
| 0 | Every capture taken with the rig verified on both sides. |
| 2 | A field would not take, an `--expect` failed, or a row moved. |
| 3 | `m8_capture` failed. |

## `level_measure.py` — steady-state level

```powershell
python tools/level_measure.py hwtest_out/level/lim80_*.wav
```

Prints peak, RMS, crest, ripple and the window it used. Defaults to the mean
over 100-420 ms **from the note's own onset**.

## `step_cell.py` — a cell the field map cannot name

```powershell
python tools/step_cell.py --screen MIXER --from-field DJF_FREQ --keys DOWN `
    --row-re "OTT ([0-9A-F]{2})" --to 40
```

Reach the cell by naming a field the map *does* know and pressing on from there;
identify the value by a regex over the decoded **row**. `--enum` steps one at a
time and reverses on a stall, for enum cells whose lists do not wrap.

## Gotchas — every one of these produced a wrong reading first

- **Keyjazz velocity is not a level control on every instrument**, though
  [`m8_capture`](m8_capture.md) calls `--keyjazz-vel` "the level lever". On the
  `WAVV7F` WAVSYNTH probe, `0x40` and `0x7F` gave peak 0.3478 both times. That
  instrument's volume is its AHD envelope; nothing routes velocity to it. Check
  before building a sweep on it.
- **Instrument `AMP` is a drive, not a gain.** `00`→`FF` moves the output 2.7 dB
  in total, nearly all in the first step, and drops crest from 3.15 to 2.55 dB.
- **A fixed measurement window is wrong.** `m8_capture` trims to the first
  sample over 0.01, so a capture below about −40 dBFS peak never triggers the
  trim and starts wherever the recording did. The four quietest reference points
  showed an 11-34 dB spread across a fixed window and 0.03 dB across a
  note-relative one. Nothing was wrong with the tone.
- **Compare rows with whitespace collapsed.** The decoder re-spaces the same
  values between reads — `m8drv` records `" OUTPUT VOL  F0"` and `"OUTPUTVOLF0"`
  for one field. A raw compare discarded a run whose every value was identical
  because `SHAPE` rendered as `"SHAPE   06SINE"` and then `"SHAPE  06SINE"`.
  `norm()` in `level_run.py` and `rt60_run.py` does this; anything else
  comparing decoded rows needs it too.
- **Read the rows, not the whole dump.** `dump`'s header carries a `cursor :`
  line holding the same label and value, so a regex over the raw output can
  match the cursor readout instead of the row. A `DEST` step reported reaching
  `01` while the row still read `00`.
- **`GOTO` does not home the cursor.** The M8 remembers a per-screen cursor
  position, so a key path counted from "the top" only works if you saturate
  first. This is the same trap as the FM screen's DOWN chain
  ([`fm_probe`](fm_probe.md)).
- **`--drop-db` needs raising when the control under test reduces level.** The
  note-finder calls "the note" everything within 6 dB of the loudest bin; under
  9 dB of gain reduction the settled tail falls outside that and the note looks
  too short to measure.
