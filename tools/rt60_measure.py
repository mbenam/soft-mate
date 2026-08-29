#!/usr/bin/env python3
"""Reverb decay time from a capture, three ways, so they can disagree in public.

For captures_backlog A1. The input is a `rt60_run.py` capture: a short keyjazz
note into a 100%-wet reverb (instrument DRY 00 / REV FF), the note released with
`m8_capture --note-ms`, and the tail recorded for the rest of the window.

Three numbers, deliberately not one:

  slope_db_s   least-squares slope of the 10 ms RMS envelope in dB against time,
               fitted between --fit-hi and --fit-lo dB below the level at
               note-off. RT60_slope = 60 / slope.
  rt60_schr    Schroeder backward integration of the same region, T30 (-5 dB to
               -35 dB on the energy-decay curve) extrapolated to 60 dB.
  t60_direct   measured, not extrapolated: time from note-off until the 10 ms RMS
               envelope first falls 60 dB below its value at note-off. Available
               because the M8's USB tap floor is exact digital zero, so 60 dB of
               range is really there.

If the three disagree by more than a little, the decay is not exponential and no
single RT60 describes it -- say so rather than picking one.

    python tools/rt60_measure.py hwtest_out/rev/decC0.wav --note-ms 300
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
import wave


def read_mono(path):
    with wave.open(path, "rb") as w:
        n, ch, sr, sw = w.getnframes(), w.getnchannels(), w.getframerate(), w.getsampwidth()
        if sw != 2:
            raise SystemExit("%s: expected 16-bit PCM" % path)
        raw = w.readframes(n)
    d = struct.unpack("<%dh" % (n * ch), raw)
    if ch == 1:
        return [x / 32768.0 for x in d], sr
    return [(d[i * ch] + d[i * ch + 1]) / 2.0 / 32768.0 for i in range(n)], sr


def envelope_db(x, sr, win_ms=10.0):
    """Non-overlapping RMS envelope in dBFS, plus the bin centre times."""
    b = max(1, int(sr * win_ms / 1000.0))
    db, t = [], []
    for k in range(0, len(x) - b + 1, b):
        s = 0.0
        for v in x[k:k + b]:
            s += v * v
        db.append(10.0 * math.log10(s / b + 1e-30))
        t.append((k + b / 2.0) / sr)
    return t, db


def lsq(xs, ys):
    n = len(xs)
    mx = sum(xs) / n
    my = sum(ys) / n
    num = sum((a - mx) * (b - my) for a, b in zip(xs, ys))
    den = sum((a - mx) ** 2 for a in xs)
    return (num / den if den else float("nan")), my - (num / den if den else 0.0) * mx


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--note-ms", type=float, default=300.0,
                    help="when the excitation stopped, ms from the start of the file")
    ap.add_argument("--settle-ms", type=float, default=60.0,
                    help="skip this much after note-off before calling it decay")
    ap.add_argument("--fit-hi", type=float, default=-5.0)
    ap.add_argument("--fit-lo", type=float, default=-35.0)
    a = ap.parse_args()

    x, sr = read_mono(a.wav)
    t, db = envelope_db(x, sr)
    t0 = (a.note_ms + a.settle_ms) / 1000.0
    i0 = next((i for i, v in enumerate(t) if v >= t0), None)
    if i0 is None:
        raise SystemExit("%s: file ends before the note does" % a.wav)

    ref = db[i0]
    peak = max(abs(v) for v in x)

    # slope fit
    fit = [(t[i] - t[i0], db[i]) for i in range(i0, len(t))
           if ref + a.fit_lo <= db[i] <= ref + a.fit_hi]
    if len(fit) < 20:
        slope = float("nan")
        rt_slope = float("nan")
    else:
        m, _ = lsq([p[0] for p in fit], [p[1] for p in fit])
        slope = -m
        rt_slope = 60.0 / slope if slope > 0 else float("nan")

    # Schroeder backward integration over the decay region
    tail = x[int(t0 * sr):]
    acc = 0.0
    edc = [0.0] * len(tail)
    for i in range(len(tail) - 1, -1, -1):
        acc += tail[i] * tail[i]
        edc[i] = acc
    tot = edc[0] if edc else 0.0
    rt_schr = float("nan")
    ed = []
    if tot > 0:
        ed = [10.0 * math.log10(v / tot + 1e-30) for v in edc]
        pts = [(i / sr, ed[i]) for i in range(len(ed)) if -35.0 <= ed[i] <= -5.0]
        if len(pts) > sr // 100:
            m, _ = lsq([p[0] for p in pts], [p[1] for p in pts])
            if m < 0:
                rt_schr = 60.0 / (-m)

    # Schroeder EDC crossing times. Monotone by construction, so unlike a raw
    # envelope these cannot be tripped by one quiet 10 ms bin -- and because the
    # floor here is exact digital zero, the -65 dB crossing is a MEASURED 60 dB
    # of decay, not an extrapolation.
    cross = {}
    if tot > 0:
        for lvl in (-5, -15, -25, -35, -45, -55, -65):
            for i in range(len(ed)):
                if ed[i] <= lvl:
                    cross[lvl] = i / sr
                    break
    t60 = float("nan")
    if -5 in cross and -65 in cross:
        t60 = cross[-65] - cross[-5]

    # time to exact digital silence
    last = 0
    for i, v in enumerate(x):
        if v != 0.0:
            last = i
    silence = last / sr

    print("%-28s peak %.4f  ref(note-off+%.0fms) %.2f dBFS" %
          (a.wav.split("\\")[-1].split("/")[-1], peak, a.settle_ms, ref))
    print("   slope        %8.3f dB/s   -> RT60 %6.3f s   (fit %.0f..%.0f dB, %d bins)"
          % (slope, rt_slope, a.fit_hi, a.fit_lo, len(fit)))
    print("   Schroeder T30 extrapolated -> RT60 %6.3f s" % rt_schr)
    print("   EDC -5 dB to -65 dB, measured      %6.3f s   (a real 60 dB, no extrapolation)" % t60)
    print("   EDC crossings (s from note-off+settle): " +
          "  ".join("%d:%.3f" % (k, v) for k, v in sorted(cross.items(), reverse=True)))
    print("   last non-zero sample at            %6.3f s (file %.2f s)" % (silence, len(x) / sr))
    return 0


if __name__ == "__main__":
    sys.exit(main())
