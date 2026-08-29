#!/usr/bin/env python3
"""Steady-state level of a capture: peak, RMS, crest and ripple, in dBFS.

For captures_backlog A6. A transfer curve is output level against input level,
so what matters is that both are measured the same way over a window where the
tone is genuinely steady -- and that a capture which is NOT steady shows up as
such instead of being averaged into a plausible number.

**The window is note-relative, not absolute.** `m8_capture` trims to the first
sample over 0.01, so a capture quieter than about -40 dBFS peak never triggers
the trim and begins wherever the recording did. Measured on the reference sweep:
the four quietest points showed an 11-34 dB spread across a fixed 0.05-0.45 s
window against 0.1 dB for the loud ones, which is a misaligned window and not an
unsteady tone. So the note is located first -- the first 20 ms bin within 6 dB of
the loudest -- and the window is taken relative to that.

**RMS is a power average and `ripple` is printed beside it**, because the OTT
compressor does not hold a flat level: it applies a stationary periodic gain
modulation with an ~80 ms period and 0.5-1.2 dB of swing, stable from about
60 ms after the note starts. That is settled behaviour rather than a note still
moving, so averaging over whole periods is the right measurement -- but one
number alone would hide it, so both are printed.

`crest` separates a steady tone from anything reshaped, and `clipped` counts
samples at or over full scale: a clipped capture is not a data point at all.

    python tools/level_measure.py hwtest_out/level/lim80_*.wav
"""
from __future__ import annotations

import argparse
import math
import sys
import wave

import numpy as np

BIN_MS = 20.0


def read_mono(path):
    with wave.open(path, "rb") as w:
        ch, sr, n = w.getnchannels(), w.getframerate(), w.getnframes()
        if w.getsampwidth() != 2:
            raise SystemExit("%s: expected 16-bit PCM" % path)
        raw = w.readframes(n)
    d = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    if ch > 1:
        st = d.reshape(-1, ch)
        return st.mean(axis=1), st, sr
    return d, d.reshape(-1, 1), sr


def db(x):
    return 20.0 * math.log10(x) if x > 0 else float("-inf")


def bins_db(x, sr):
    b = int(sr * BIN_MS / 1000.0)
    n = len(x) // b
    if n < 3:
        return None, b
    r = np.sqrt(np.mean(x[:n * b].reshape(-1, b) ** 2, axis=1))
    return np.where(r > 0, 20.0 * np.log10(r + 1e-30), -300.0), b


def note_window(x, sr, skip_ms, span_ms, drop_db=6.0):
    """[start+skip, start+skip+span] inside the note, or (None, None)."""
    lvl, b = bins_db(x, sr)
    if lvl is None:
        return None, None
    on = np.flatnonzero(lvl >= lvl.max() - drop_db)
    if len(on) == 0:
        return None, None
    first, last = int(on[0]), int(on[-1])
    lo = first * b + int(sr * skip_ms / 1000.0)
    hi = lo + int(sr * span_ms / 1000.0)
    if hi > (last + 1) * b:
        return None, None
    return lo, hi


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav", nargs="+")
    ap.add_argument("--skip-ms", type=float, default=100.0,
                    help="from the note's start, how much to skip past the attack")
    ap.add_argument("--span-ms", type=float, default=320.0,
                    help="length of the measured window; whole ripple periods")
    ap.add_argument("--drop-db", type=float, default=6.0,
                    help="how far below the loudest bin still counts as the note. "
                         "Raise it when the control under test reduces the tail by "
                         "more than this, or the note looks shorter than it is")
    ap.add_argument("--quiet-header", action="store_true")
    a = ap.parse_args()

    if not a.quiet_header:
        print("%-24s %9s %9s %7s %8s %8s %13s" %
              ("file", "peak dB", "rms dB", "crest", "ripple", "clipped", "window ms"))
    for path in a.wav:
        mono, stereo, sr = read_mono(path)
        lo, hi = note_window(mono, sr, a.skip_ms, a.span_ms, a.drop_db)
        name = path.replace("\\", "/").split("/")[-1]
        if lo is None:
            print("%-24s  note too short for a %.0f+%.0f ms window (file %.2f s)" %
                  (name, a.skip_ms, a.span_ms, len(mono) / sr))
            continue
        seg = mono[lo:hi]
        peak = float(np.max(np.abs(seg)))
        rms = float(np.sqrt(np.mean(seg ** 2)))
        lvl, _ = bins_db(seg, sr)
        ripple = float(lvl.max() - lvl.min()) if lvl is not None else float("nan")
        clipped = int(np.count_nonzero(np.abs(stereo[lo:hi]) >= 0.999))
        print("%-24s %9.3f %9.3f %7.2f %8.2f %8d %5.0f-%-6.0f" %
              (name, db(peak), db(rms), db(peak) - db(rms), ripple, clipped,
               lo / sr * 1000, hi / sr * 1000))
    return 0


if __name__ == "__main__":
    sys.exit(main())
