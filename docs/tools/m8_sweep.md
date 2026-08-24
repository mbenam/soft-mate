# m8_sweep

**Source:** [`src/tools/main_sweep.cpp`](../../src/tools/main_sweep.cpp)
**Build target:** `m8_sweep` (CMakeLists.txt, links `m8_device` **and** `m8_audiocap`)
**Category:** unattended measurement — set a field, play a note, capture, measure, next value.
**Links:** `m8_device` to drive, `m8_audiocap` to record, `audio/Metrics.h` to measure. No engine,
no SDL.

## Why it exists

The same loop was hand-written three times on 2026-08-24 — sweeping REPITCH `STEPS`, sweeping the
`REP.BPM` byte, sweeping FILTER cutoff — and each rewrite carried its own bugs:

- one measured the **sequencer's row rate** instead of the sampler's loop, and "passed" while
  measuring the wrong thing (at `STEPS 0x40` the two are both a quarter beat)
- one read the **wrong screen row**, because a short label collided by substring
- one **left a play mode reset**, because a field lookup matched the row above

None of that was hard and all of it was avoidable. This is that loop, written once.

## Designed to be run, not watched

Every value is verified by read-back before the capture, the field is restored at the end, and
the result is a table plus an exit code. That is deliberate: **running it needs a scheduled
command, not an agent.** The judgement is in choosing the sweep and reading the table, and that
stays with a person — this session produced two confident wrong measurements from good-looking
data, so the interpretation is exactly where a machine should not be trusted.

## CLI flags

| Flag | Required | Meaning |
|---|---|---|
| `--port <name>` | no (`COM3`) | Serial port. |
| `--audio <substr>` | no (`M8`) | Capture device whose name contains this. |
| `--field <NAME>` | no (`CUTOFF`) | The field to sweep. |
| `--values <a,b,c>` | no | Comma-separated hex values, in sweep order. |
| `--out-dir <dir>` | no (`sweep_out`) | Where the WAVs and manifests go. |
| `--seconds <n>` | no (`2`) | Capture length per value. |
| `--note <n>` | no (`60`) | MIDI note to keyjazz. |
| `--hold-ms <n>` | no (`20`) | Key hold duration. |
| `--period` | no | Also measure the repeat period (autocorrelation). Off by default — it is the slower metric and only means something for looping material. |
| `--allow-mutation` | **yes** | Required; the tool edits a field. |
| `--help` | no | Print usage and exit 0. |

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Every value set, captured and measured, and the field restored. |
| 1 | At least one value failed to set, **or the field was not restored** — the table says which. |
| 2 | Setup failed: port, audio device, or gestures not pinned. |

A failed restore is exit 1 on purpose. A sweep that leaves the instrument somewhere else silently
poisons whatever is measured next, which is how a play mode got left reset and an `AMP` left at
`5F` earlier the same day.

## Output

One WAV plus manifest per value in `--out-dir`, and a table:

```
value    read back            peak    low    mid   high pitch(Hz)   period
20       CUTOFF20           0.4176   0.66   0.60   0.23     910.6        0
```

`low`/`mid`/`high` are band-energy **ratios**, not levels — a filter shape shows up as a ratio,
while absolute level moves with the source and the sends. They come from `audio/Metrics.h`, shared
with `m8_analyze`, so the measurement is not re-implemented per investigation.

## Gotchas

- **Band ratios are a shape indicator, not a spectrum.** Measuring FILTER 05 with them produced
  differences of 0.02–0.07 against run-to-run noise of about 0.08 — i.e. nothing. If a question
  needs better than that it needs [`m8_spectrum`](m8_spectrum.md) and an FFT, not wider corners.
- **The source matters more than the sweep.** A saw is bass-heavy and MacroSynth's `FILTERED NOISE`
  is pre-filtered — on the latter *no* filter type moved the ratios, including a real HIGHPASS. Pick
  something with energy where the question is.
- **It reports the failures it hits and keeps going.** A value that will not set is a table row, not
  an abort, so one bad point does not cost the whole sweep.
- **`editValue` cannot set every field.** Enums and decimal-display fields are refused fast by
  design, and `CUTOFF` on a MacroSynth fails outright — see `M8_DRIVER_BUGS.md` #35, which this
  tool found on its second run.
- **COM3 is exclusive.** Let any m8drv daemon exit first.

## Example

```powershell
build\Release\m8_sweep.exe --port COM3 --audio M8 --field CUTOFF --values 20,40,60,80,A0,C0,E0 --out-dir sweep_cutoff --allow-mutation
```
