#!/usr/bin/env python3
"""Fundamental of a sustained capture, to a fraction of a cent.

`tools/pitch_windows.py` picks an integer autocorrelation lag, which at middle C
is a 5-cent grid -- fine for telling C-4 from anything else, not fine for "did
this come back at exactly the played note?", which is what captures_backlog B1
turns on. `m8_analyze --pitch` returned 0.000 Hz on these captures outright.

Autocorrelation via FFT, parabolic interpolation around the peak, cross-checked
against a zero-crossing count over the same window. If the two disagree by more
than a few cents the waveform is not simply periodic and the number should not
be trusted; both are printed so that is visible.

    python tools/fm_pitch.py hwtest_out/fm/pit80.wav --ref 261.63
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
import wave

import numpy as np


def read_mono(path):
    with wave.open(path, "rb") as w:
        ch, sr, n = w.getnchannels(), w.getframerate(), w.getnframes()
        if w.getsampwidth() != 2:
            raise SystemExit("%s: expected 16-bit PCM" % path)
        raw = w.readframes(n)
    d = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    if ch > 1:
        d = d.reshape(-1, ch).mean(axis=1)
    return d, sr


def autocorr_hz(x, sr, lo=50.0, hi=3000.0):
    x = x - x.mean()
    n = int(2 ** math.ceil(math.log2(len(x) * 2)))
    f = np.fft.rfft(x, n)
    ac = np.fft.irfft(f * np.conj(f), n)[:len(x)]
    if ac[0] <= 0:
        return 0.0
    ac = ac / ac[0]
    lo_lag = max(2, int(sr / hi))
    hi_lag = min(len(ac) - 2, int(sr / lo))
    seg = ac[lo_lag:hi_lag]
    k = int(np.argmax(seg)) + lo_lag
    # parabolic interpolation on the three points around the peak
    y0, y1, y2 = ac[k - 1], ac[k], ac[k + 1]
    denom = (y0 - 2 * y1 + y2)
    delta = 0.5 * (y0 - y2) / denom if denom != 0 else 0.0
    return sr / (k + delta)


def zerocross_hz(x, sr):
    x = x - x.mean()
    s = np.signbit(x)
    idx = np.flatnonzero(s[:-1] & ~s[1:])          # negative -> positive
    if len(idx) < 3:
        return 0.0
    # linear interpolation of each crossing instant, then a least-squares fit of
    # crossing index against time -- robust to one missed or extra crossing.
    x0 = x[idx]
    x1 = x[idx + 1]
    t = (idx + x0 / (x0 - x1)) / sr
    k = np.arange(len(t))
    slope = np.polyfit(t, k, 1)[0]
    return float(slope)


def spectral_hz(x, sr):
    """Interpolated frequency of the loudest FFT bin, and how dominant it is.

    Needed because autocorrelation locks onto a subharmonic once the tone gets
    high -- at MOD1 0x40 it reported 959 Hz for a tone the zero-crossing count
    and the spectrum both put at 10548 Hz. Three estimators, printed together.
    """
    w = x * np.hanning(len(x))
    mag = np.abs(np.fft.rfft(w))
    f = np.fft.rfftfreq(len(x), 1.0 / sr)
    k = int(np.argmax(mag))
    if k == 0 or k >= len(mag) - 1:
        return 0.0, 0.0
    y0, y1, y2 = np.log(mag[k - 1] + 1e-30), np.log(mag[k] + 1e-30), np.log(mag[k + 1] + 1e-30)
    d = (y0 - 2 * y1 + y2)
    delta = 0.5 * (y0 - y2) / d if d != 0 else 0.0
    hz = (k + delta) * sr / len(x)
    # how much of the total energy sits within +-3 bins of that peak
    lo, hi = max(0, k - 3), min(len(mag), k + 4)
    frac = float((mag[lo:hi] ** 2).sum() / ((mag ** 2).sum() + 1e-30))
    return hz, frac


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav", nargs="+")
    ap.add_argument("--from", dest="t0", type=float, default=0.5)
    ap.add_argument("--to", dest="t1", type=float, default=2.5)
    ap.add_argument("--ref", type=float, default=261.63,
                    help="reference Hz for the semitone/cent columns (middle C)")
    a = ap.parse_args()

    print("%-22s %10s %10s %10s %9s %6s %7s" %
          ("file", "acorr Hz", "zcross Hz", "spec Hz", "spec st", "domin", "peak"))
    for path in a.wav:
        x, sr = read_mono(path)
        seg = x[int(a.t0 * sr):int(a.t1 * sr)]
        if len(seg) < sr // 4:
            print("%-24s  too short" % path)
            continue
        hz = autocorr_hz(seg, sr)
        zc = zerocross_hz(seg, sr)
        sp, frac = spectral_hz(seg, sr)
        st = 12.0 * math.log2(sp / a.ref) if sp > 0 else float("nan")
        print("%-22s %10.3f %10.3f %10.3f %9.3f %6.2f %7.4f" %
              (path.replace("\\", "/").split("/")[-1], hz, zc, sp, st, frac,
               float(np.max(np.abs(seg)))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
