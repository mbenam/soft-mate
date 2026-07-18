# m8_capture

**Source:** [`src/tools/main_capture.cpp`](../../src/tools/main_capture.cpp) (532 lines)
**Build target:** `m8_capture` (CMakeLists.txt)
**Category:** real-hardware audio capture (the capture half of the render→capture→analyze/
spectrum hardware-parity loop)
**Links:** `miniaudio` (header-only, vendored). No SDL, no engine. Standalone — has its own
independent Win32 serial implementation (not shared with [`m8_nav`](m8_nav.md)'s `M8Device`; both
speak the same raw M8 controller protocol but are separate codebases).

## What it does

Drives a real M8 headless over serial (button presses — start/stop playback, or live `keyjazz`
notes) while simultaneously recording its USB audio output, then trims the recording to the note
onset and writes a WAV. **It assumes the probe/song is already loaded on the device** — this tool
has no file-loading capability of its own (that's what `m8_nav --load-file` is for; see
[`m8_nav`](m8_nav.md)). In batch mode, loading between captures is an explicit human step (see
below) — not automated by this tool.

## The M8 serial protocol (as this tool speaks it)

- `'E'` — enable display (sent once at startup, 100ms settle).
- `'C' <mask>` — set button state; `'C' 0x00` releases. **`PLAY` is a toggle** (start and stop
  are the *same* mask, `0x08` by default) — there's no separate stop command, pressing PLAY again
  while playing stops it.
- `'K' <note> <vel>` — keyjazz note-on (plays a live note directly on the synth engine, bypassing
  the song/sequencer entirely — a from-scratch single note for synth-parity testing).
  `'K' 0xFF` — keyjazz note-off.

The header comment block in the source (`main_capture.cpp` lines 118-136) still contains an
earlier, since-superseded guess at the bit layout — the mask constants actually used
(`startMask`/`stopMask` default `0x08`) are the empirically-pinned values from
`M8_HARDWARE_TEST_SPEC.md` §5; trust the CLI defaults and the code, not that comment block.

## CLI flags

| Flag | Default | Meaning |
|---|---|---|
| `--port <name>` | *(required)* | Serial port, e.g. `COM4`. Tool exits with usage text if omitted. |
| `--audio <substring>` | `M8` | Substring match against capture device names (via `ma_context_get_devices`) — picks the first device whose name contains this. If no match, prints the full device list to stderr and exits 1. |
| `--out <path>` | — | Single-shot mode: output WAV path. |
| `--out-dir <dir>` | — | Single-shot mode: output directory (filename auto-generated from a millisecond timestamp, `capture_<ms>.wav`) if `--out` isn't given; **required** for `--batch`. |
| `--batch <path>` | — | Batch mode: path to a `name<TAB>label` list file (see below). |
| `--seconds <n>` | `3.0` | Recording duration per capture. |
| `--pre-roll <ms>` | `5.0` | Milliseconds of audio kept *before* the detected onset. |
| `--tail <n>` | `0.0` (no trim) | If > 0, hard-caps the trimmed output to this many seconds from onset. |
| `--start-mask <hex>` | `0x08` | Override the PLAY-toggle mask (should not normally need changing — this is empirically pinned). |
| `--stop-mask <hex>` | `0x08` | Same value as start by default (PLAY is a toggle, not separate start/stop keys). |
| `--keyjazz <note>` | *(none — uses PLAY toggle)* | MIDI note number (60 = C-4). If set (`>= 0`), captures via keyjazz note-on/off instead of PLAY start/stop — plays one live note, not the sequencer. |
| `--keyjazz-vel <n>` | `0x7F` | Velocity for keyjazz mode. |

## Batch file format (`--batch`)

Plain text, one entry per line: `name<TAB>label`. Blank lines and lines starting with `#` are
skipped. `name` is purely informational (printed in the "load this now" prompt); `label` becomes
the output filename (`<out-dir>/<label>.wav`). **The whole file is validated up front** (missing
tabs, unreadable file) before touching serial or audio, so a typo fails immediately rather than
after you've already started the run.

For each entry, the tool prints `Load '<name>' on the device now.` and **blocks on stdin**
(`std::getline(std::cin, dummy)`) waiting for Enter — this is a real human-in-the-loop step, not
automated. The serial connection and audio device are opened exactly once for the whole batch
(not per-entry), so switching probes between captures is the only manual action required.

## Capture mechanics (`captureOnce`)

1. Clear the shared frame buffer, wait 200ms.
2. Trigger playback: either `keyjazz` note-on, or PLAY-toggle start.
3. Sleep for `--seconds`.
4. Stop: `keyjazz` note-off, or PLAY-toggle again; wait another 200ms.
5. Snapshot the accumulated frames (captured continuously via a `miniaudio` callback into a
   mutex-guarded buffer since capture started — the device runs for the whole process, not
   per-capture, so this step is just "read what's accumulated since step 1's buffer clear").
6. **Trim to onset**: first frame where either channel's absolute value exceeds `0.01`, minus
   `--pre-roll` milliseconds. If nothing ever crosses the threshold, `onset = 0` (whole buffer
   kept from the start — this is what a near-silent capture looks like: no meaningful trim
   happens, and the resulting WAV mostly reflects the pre-onset noise floor).
7. If `--tail > 0`, hard-truncate to that many seconds from the trim start.

Output is always 48kHz stereo 16-bit PCM (same `writeWav` as `m8_render`, clamped to `[-1,1]`
before quantizing).

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Ran to completion — **including if nothing was written** (see Gotchas) |
| 1 | No `--port`; `--batch` without `--out-dir`; malformed/unreadable batch file; serial port open failure; miniaudio context/device init failure; no matching audio device found |

## Examples

```
# Single capture, PLAY-toggle
m8_capture --port COM4 --audio "M8" --seconds 2.5 --out ref.wav

# Single capture via keyjazz (bypasses the sequencer, plays one live note)
m8_capture --port COM4 --audio "M8" --keyjazz 60 --keyjazz-vel 127 --seconds 2 --out c4.wav

# Batch: capture a list of probes, prompting for a manual reload between each
m8_capture --port COM4 --audio "M8" --batch probes.txt --out-dir refs/
```
`probes.txt`:
```
# name<TAB>label
probe_macro_00.m8s	macro_shape00
probe_macro_10.m8s	macro_shape10
```

## Gotchas

- **Batch mode is not unattended.** It blocks on stdin per entry, waiting for you to physically
  reload the named probe on the device and press Enter. Compare to `m8_nav --load-file`, which
  *can* drive the load automatically — this tool doesn't call into that, so combining the two
  into a fully unattended batch loop would need external orchestration.
- **Silent/near-silent output does not fail this tool.** If the M8 never produces audio above the
  0.01 onset threshold during the capture window (recording-level issue, wrong device selected,
  instrument genuinely silent), `trimToOnset` just returns `onset=0` and writes whatever was
  captured — a full window of near-noise-floor audio, not an error. Check the written file with
  [`m8_analyze`](m8_analyze.md) (peak/RMS) to catch this; this tool won't tell you.
- **The known "recording level reset" gotcha** (`hw-test-rig` memory): a device power-cycle can
  reset the host's Windows recording level for the M8 input, making every capture ~100× too
  quiet (peak ≈ 0.006 for a full-scale signal) even though the device itself is producing normal
  output. Fix is host-side (Windows Sound settings → Recording → the M8 input → Levels), not in
  this tool. The M8's own `OUTPUT VOL` mixer setting does **not** affect this — it feeds
  headphone/line out, not the USB audio tap this tool reads.
- **If neither `--out` nor `--out-dir` is given in single-shot mode, the tool still runs the full
  capture cycle (presses buttons, waits, records) and only then prints `no output path specified`
  to stderr — and still exits 0.** Nothing is written, but the process doesn't fail; don't rely
  on the exit code to catch this, check that the expected file actually exists.
- **`--keyjazz` and PLAY-toggle mode are mutually exclusive** per invocation (keyjazz is used if
  `keyjazzNote >= 0`, i.e. any `--keyjazz` value was passed) — you can't capture "play the
  sequencer AND also send a keyjazz note" in one call.
