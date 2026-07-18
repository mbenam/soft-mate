# m8_analyze

**Source:** [`src/tools/main_analyze.cpp`](../../src/tools/main_analyze.cpp) (512 lines)
**Build target:** `m8_analyze` (CMakeLists.txt)
**Category:** offline audio analysis (the analyze half of the render→analyze→spectrum closed
loop; the machine-readable pass/fail gate for "is this audio actually correct")
**Links:** `m8_engine` only (for `analysis::AudioMetrics`). No SDL.

## What it does

Reads a rendered WAV (stereo, 16-bit PCM — the exact format [`m8_render`](m8_render.md) writes),
computes a fixed set of objective health metrics via `m8::analysis::analyze()`, prints them, runs
them against **hard pass/fail thresholds**, and exits non-zero if any threshold fails. This is
the thing that turns "does it sound okay" into a CI-checkable assertion.

Three modes, selected by which flags are present:

1. **Plain analysis** (`m8_analyze file.wav`) — metrics + hard-check pass/fail.
2. **Per-note analysis** (`--events file_events.csv`) — additionally measures pitch, spectral
   centroid (start vs. end, i.e. does the tone brighten/darken), and attack time **per note**,
   using the events CSV `m8_render` writes as ground truth for where each note starts.
3. **Diff mode** (`m8_analyze --diff a.wav b.wav`) — sample-by-sample byte-exact comparison of
   two WAVs, independent of the metrics/hard-check machinery entirely.

## Hard checks (plain/per-note modes)

These are the checks that actually set the exit code — everything else printed is informational.

| Check | Threshold | Rationale |
|---|---|---|
| `peak < 1.0` | fails if `peak >= 1.0` | true digital full-scale, distinct from `clipped` below |
| `clipped == 0` | fails if any sample `>= 0.999` (per `AudioMetrics`'s own clip definition) | |
| `non_finite == 0` | fails if any NaN/Inf sample | |
| `\|DC_L\| < 0.005` and `\|DC_R\| < 0.005` | overall per-channel DC offset | |
| `dc_worst_window < 0.01` | worst 1-second-window DC offset (catches transient DC that averages out over the whole file) | |
| `crest_db > 6.0` | peak-to-RMS ratio in dB — too low crest means the signal isn't dynamic (e.g. stuck at a flat level, or the "note fails to actually sound" class of bug) | |

Note the strict inequalities: `peak >= 1.0` fails but `peak == 0.999` passes; `crest_db <= 6.0`
fails but `6.01` passes. These exact boundaries matter if you're tuning a probe or debugging a
borderline pass — read the printed FAIL line, it states which check and by how much.

## CLI flags

| Flag | Meaning |
|---|---|
| `<file.wav>` (positional, first non-flag arg) | The WAV to analyze. Required unless `--diff` is used. |
| `--events <path>` | Enables per-note analysis using the given events CSV (see below). |
| `--json <path>` | Also write a machine-readable JSON report (see schema below). |
| `--diff <a.wav> <b.wav>` | Switches to diff mode entirely — ignores all other flags, compares two files sample-by-sample. |

## Per-note analysis (`--events`)

Parses the CSV `m8_render` writes (`sample_time,seconds,type,track,song_row,chain_row,
phrase_row,instrument,frequency,volume`), keeps only `NOTE_ON` rows, and groups them by track.
For each note, the analysis window is `[this note's sample_time, the next NOTE_ON's sample_time
on the same track)` — matching how the M8's own hardware-test spec defines a note window. **This
is only meaningful for a solo render or a probe song** where the note's track is the dominant
(ideally only) source in that window; on a full mix it'll measure whatever the *sum* of
everything sounding in that window looks like.

Per note, three independently-computed measurements (each has its own minimum-window-length
gate; a very short note prints `n/a` for whichever measurement didn't have enough samples):
- **Pitch** — skips a short attack transient (`min(500 samples, window/4)`), measures pitch over
  the sustain (capped at 1s), reports both measured Hz and the deviation from the expected
  frequency in cents (`1200·log2(measured/expected)`). Requires ≥256 samples of sustain.
  Uses `m8::analysis::pitchHz`, seeded with the expected frequency.
- **Spectral centroid, start vs. end** — brightness at note-start vs. note-end (does a filter
  sweep/decay actually move the spectrum). Two independent windows (up to 4096 samples each, from
  near the start and from the very end of the note). Requires ≥64 samples.
- **Attack time** — finds the peak of a short-window (~0.67ms hop) RMS envelope, searched over up
  to 2 seconds of the note. Reported as milliseconds from note-on to that peak.

Printed as a table (`printNoteReport`) sorted chronologically (not grouped by track, even though
internally it's computed per-track group).

## JSON report schema (`--json`)

```jsonc
{
  "file": "...", "channels": 2, "sample_rate": 48000, "frames": N, "duration_sec": 0.0,
  "metrics": {
    "peak": 0.0, "rms": 0.0, "crest_db": 0.0, "clipped": 0, "non_finite": 0,
    "dc_l": 0.0, "dc_r": 0.0, "dc_worst_window": 0.0,
    "mid_rms": 0.0, "side_rms": 0.0, "correlation": 0.0, "longest_silence_sec": 0.0
  },
  "hard_checks": {
    "peak_ok": true, "clipped_ok": true, "non_finite_ok": true, "dc_ok": true,
    "dc_worst_window_ok": true, "crest_ok": true, "overall_pass": true
  },
  // only present if --events was also given:
  "events_file": "...",
  "notes": [
    { "sample_time": N, "track": 0, "expected_hz": 0.0,
      "measured_hz": 0.0, "pitch_cents": 0.0,           // only if pitch was valid
      "centroid_start_hz": 0.0, "centroid_end_hz": 0.0, // only if centroid was valid
      "attack_ms": 0.0 }                                 // only if attack was valid
  ]
}
```
`hard_checks`' individual booleans are computed independently in `writeJsonReport`, not derived
from `checkHard()`'s result — but the thresholds match exactly (verified by reading both: e.g.
`crest_ok` uses `crestDb > 6.0f`, same boundary `checkHard` gates on). `overall_pass` is `ok`,
`checkHard()`'s actual return value, so it's authoritative for the exit code even though the
per-field booleans are a parallel computation.

## Diff mode (`--diff a.wav b.wav`)

Reads both WAVs, requires matching channel count and sample rate (fails immediately, exit 1, if
not), then compares every sample up to `min(framesA, framesB)`. Reports `max |A-B|` and the exact
sample/frame/channel of the *first* difference (with both values), plus a note if the files have
different lengths (only the overlapping prefix is compared). **PASS requires byte-identical
audio AND identical frame counts** — a file that's a perfect prefix match but longer/shorter than
the other still reports `FAIL`.

## Exit codes

| Code | Mode | Meaning |
|---|---|---|
| 0 | plain/events | all hard checks passed |
| 1 | plain/events | at least one hard check failed |
| 2 | plain/events | usage error (no file given, unreadable WAV, wrong channel count — must be exactly 2) |
| 0 | diff | files are byte-identical and same length |
| 1 | diff | files differ (content or length), or channel/rate mismatch |
| 2 | diff | usage error or unreadable file |

## Examples

```
# Basic health check
m8_analyze render.wav

# With per-note pitch/attack analysis
m8_analyze render.wav --events render_events.csv

# Machine-readable report for CI
m8_analyze render.wav --json report.json

# Byte-exact regression check between two renders
m8_analyze --diff golden.wav candidate.wav
```

## Gotchas

- **Requires exactly 2 channels.** A mono WAV is rejected outright (exit 2) — this tool assumes
  the stereo output `m8_render`/`m8_capture` produce.
- **`--events` per-note metrics are only meaningful when the named track is the dominant source
  in its window.** On a full song mix with multiple tracks overlapping, the "measured pitch"
  will reflect whatever's actually sounding, not necessarily the named track's note.
- **Diff mode requires identical length to PASS**, even if every overlapping sample matches —
  don't read "max |A-B| = 0.0" alone as a pass; check the final PASS/FAIL line.
- The JSON's per-field `hard_checks` booleans are a second, independent computation from the
  stdout `FAIL` lines — if you need to know exactly why a run failed, read stdout, not just the
  JSON's `overall_pass`.
