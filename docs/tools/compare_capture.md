# compare_capture

**Source:** [`tools/wavetables/compare_capture.py`](../../tools/wavetables/compare_capture.py) (Python 3)
**Build target:** none — a Python script. Needs `numpy` (WAV reading is stdlib).
**Category:** hardware verification. Compares a real-M8 capture against the wave
table frames digitised by
[`extract_manual_wavetables`](extract_manual_wavetables.md).
**Links:** nothing. Reads a `.wav` and an `M8WT` bank; prints a report.
**Spec:** [`WAVSYNTH_PHASE3_SPEC.md`](../../specs/WAVSYNTH_PHASE3_SPEC.md) §7

## What it does

Takes a capture of the real device playing one WavSynth wave table frame, folds
it down to a single averaged cycle, and reports which of that table's 64 frames
it matches. This is the tool that verified the extraction — it is how
`OSC:GRAPHIC` frame 0 was shown to match hardware at **r = 0.998**, and how the
SCAN crossfade was pinned down.

It is a **reporting tool, not a gate**: it never fails on match quality, the same
way [`m8_spectrum`](m8_spectrum.md) does not.

## Pipeline

1. **Downmix** the capture to mono and take a steady-state window (0.3s–1.5s),
   skipping the attack.
2. **Fold to one cycle.** The period is computed from the note, not estimated:
   MIDI 36 is 65.406Hz, so 734.0 samples at 48kHz. Every whole cycle in the
   window (typically ~77 of them) is resampled to 200 points and averaged. The
   averaging is what makes the comparison robust — noise and drift fall away and
   what remains is the waveform.
3. **Normalise** (remove mean, scale to unit peak) and compare against each of
   the table's 64 frames by **circular** cross-correlation, because the phase
   origin of a capture is arbitrary — the note started wherever it started.
4. With `--scan`, additionally build the crossfade the SCAN byte implies and
   report it against the two frames it sits between.

## CLI flags

| Flag | Required | Meaning |
|---|---|---|
| `wav` | yes | The capture, positional. |
| `table` | yes | Table name, positional, e.g. `OSC:GRAPHIC`. Names are the bank's own — run with a wrong one to get the list. |
| `--bank <path>` | no (default `wavetables.bin`) | The `M8WT` binary bank. Produce it with `extract_manual_wavetables --bin`. |
| `--scan <byte>` | no | The SCAN byte the capture was taken at, e.g. `0x80`. Adds the crossfade report. |
| `--midi <n>` | no (default 36) | The note that was captured. The period comes from this, so it must be right. |

## Capturing something to compare

Set the device to the table under test with **everything else neutral** — a
filter or a non-zero AMP changes the waveform and the comparison then means
nothing:

    SIZE FF   MULT 00   WARP 00   FILTER 00   AMP 00   LIM 00   PAN 80   DRY C0
    MFX 00    DEL 00    REV 00

Named-field navigation does not work on the WavSynth instrument screen
(`ScreenModel` knows only the SAMPLER and MACROSYN variants), so drive it with
raw key presses through [`m8drv`](m8drv.md) `batch`. Then:

```bash
build/Release/m8_capture.exe --port COM3 --audio "M8" --keyjazz 36 --seconds 2 --out cap.wav
```

```bash
python tools/wavetables/compare_capture.py cap.wav OSC:GRAPHIC --bank wavetables.bin --scan 0x00
```

`m8_capture` needs COM3, and `m8drv` holds it exclusively while its daemon runs —
so let the `m8drv` invocation finish before capturing.

## Reading the output

```
cap.wav: 77 cycles averaged, cycle peak 0.3816
  best-matching frames: [(0.998, 0), (0.994, 1), (0.994, 3), (0.953, 57), (0.944, 4)]
  SCAN 0x00 -> frame 0.00 (0 x 1.00 + 1 x 0.00)
    vs crossfade  : 0.9984
    vs frame 0    : 0.9984
    vs frame 1    : 0.9938
```

The claim is only strong when the expected frame is **both** a high match and the
**best** match. A high correlation on its own proves little: adjacent frames in a
smoothly-morphing table are similar to each other by construction, and several
frames scoring 0.99 is normal.

**`cycle peak` is the number that settles amplitude questions**, and correlation
is the number that settles shape questions. Keep them apart — the crossfade proof
below rests entirely on the peak.

## Gotchas

- **Correlation is reported as an absolute value, so an inverted match scores the
  same as an upright one.** This is deliberate — a capture's polarity depends on
  the output path — but it means the tool cannot tell you a frame is
  phase-flipped. For a nearly odd-symmetric waveform a half-period circular shift
  is indistinguishable from inversion, which is why one comparison in the spec
  reads as "best matched inverted" and is *not* evidence of a sign error
  (`WAVSYNTH_PHASE3_SPEC.md` P4).
- **Correlation cannot prove the SCAN crossfade; amplitude can.** If frames A and
  B are near-inversions, a 50/50 blend of them correlates strongly with *both* —
  so the `--scan` report looks ambiguous at a midpoint. What is unambiguous is
  that the blend **cancels**. The proof used `BNK:SCRATCH` frames 2 and 3, whose
  sum peaks at 0.081 of full scale, and compared captured peaks: 0.428 at SCAN
  `0x08`, **0.027** at the midpoint `0x0A`, 0.407 at `0x0C`. A frame *selector*
  cannot produce that 24dB collapse. Reach for an inverted frame pair whenever a
  blend-vs-select question comes up.
- **Use a low note.** MIDI 36 gives ~734 samples per cycle at 48kHz, so one cycle
  comfortably oversamples a 200-sample frame. At C-4 a cycle is ~184 samples and
  the frame is undersampled, which blurs exactly the high-frequency detail a
  comparison most needs.
- **`--midi` must match what was actually played.** The period is derived from
  it, not detected, so a wrong note silently smears the cycle average into
  something meaningless rather than erroring.
- **A capture is only as good as the device state.** Verify the screen with
  `m8drv dump` after setting up and before capturing — a mis-landed key press
  that leaves SIZE at `20` instead of `FF` produces a real, clean, and completely
  misleading measurement.
- **The bank path is not the engine's copy.** The engine builds
  `src/engine/data/WavetableBank.cpp`; this tool reads the optional `--bin`
  output. They come from the same extractor run, but if you regenerate one and
  not the other they can drift apart.
