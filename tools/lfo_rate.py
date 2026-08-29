#!/usr/bin/env python3
"""Modulation rate of a ModFX capture, from the pitch wobble.

For captures_backlog A2. The obvious instrument -- the amplitude envelope -- does
not work at 100% wet: with `DRY 00` there is no dry path for the delayed copy to
comb against, so the output is level-flat (measured: -17.5 dB +-0.3 dB across a
whole note at `MOD DEPTH FF`, `FRQ FF`). A chorus modulates delay time, which is
a *frequency* modulation of the wet signal, so that is what is tracked here.

Instantaneous frequency comes from the analytic signal, built by zeroing the
negative-frequency half of the FFT (no scipy needed), then unwrapping the phase
and differentiating. The result is smoothed over one carrier period and its own
dominant rate is read off an FFT of that frequency track.

    python tools/lfo_rate.py hwtest_out/level/a2frqFF_00.wav --carrier 2219.4
"""
from __future__ import annotations

import argparse
import os
import sys
import wave

import numpy as np


def read_mono(path):
    with wave.open(path, "rb") as w:
        ch, sr, n = w.getnchannels(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    d = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    if ch > 1:
        d = d.reshape(-1, ch).mean(axis=1)
    return d, sr


def track_freq(x, sr, win_ms=20.0, hop_ms=4.0, lo_hz=200.0):
    """Interpolated spectral-peak frequency per short window.

    An analytic-signal phase derivative was tried first and does not survive this
    source: the wavsynth tone carries sidebands 27-34 dB down, and the Hilbert
    track came back with a 20 Hz "carrier" and a +-697 Hz deviation on a signal
    whose real carrier is 2219 Hz. A peak tracker ignores everything that is not
    the loudest partial.
    """
    w = int(sr * win_ms / 1000.0)
    hop = max(1, int(sr * hop_ms / 1000.0))
    win = np.hanning(w)
    out, t = [], []
    for k in range(0, len(x) - w, hop):
        seg = x[k:k + w] * win
        mag = np.abs(np.fft.rfft(seg, 4 * w))
        freqs = np.fft.rfftfreq(4 * w, 1.0 / sr)
        lo = int(np.searchsorted(freqs, lo_hz))
        i = lo + int(np.argmax(mag[lo:]))
        if i <= 0 or i >= len(mag) - 1:
            continue
        y0, y1, y2 = np.log(mag[i - 1] + 1e-30), np.log(mag[i] + 1e-30), np.log(mag[i + 1] + 1e-30)
        d = y0 - 2 * y1 + y2
        delta = 0.5 * (y0 - y2) / d if d != 0 else 0.0
        out.append(float(freqs[i] + delta * (freqs[1] - freqs[0])))
        t.append((k + w / 2.0) / sr)
    return np.array(t), np.array(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav", nargs="+")
    ap.add_argument("--from", dest="t0", type=float, default=0.08)
    ap.add_argument("--to", dest="t1", type=float, default=0.48)
    ap.add_argument("--carrier", type=float, default=0.0,
                    help="expected carrier Hz; 0 = take it from the track's mean")
    ap.add_argument("--lo", type=float, default=1.0, help="lowest rate searched, Hz")
    ap.add_argument("--hi", type=float, default=120.0, help="highest rate searched, Hz")
    a = ap.parse_args()

    print("%-20s %10s %10s %10s %9s %s" %
          ("file", "carrier Hz", "dev +-Hz", "rate Hz", "cycles", "verdict"))
    for path in a.wav:
        x, sr = read_mono(path)
        seg = x[int(a.t0 * sr):int(a.t1 * sr)]
        name = os.path.basename(path)
        if len(seg) < sr // 20:
            print("%-20s  window empty" % name)
            continue
        t, fr = track_freq(seg, sr)
        if len(fr) < 16:
            print("%-20s  too few frames (%d)" % (name, len(fr)))
            continue
        fs = 1.0 / (t[1] - t[0])
        mean = float(np.mean(fr))
        dev = float(np.max(fr) - np.min(fr)) / 2.0
        d = fr - mean
        w = d * np.hanning(len(d))
        mag = np.abs(np.fft.rfft(w))
        freqs = np.fft.rfftfreq(len(d), 1.0 / fs)
        band = (freqs >= a.lo) & (freqs <= a.hi)
        if not band.any():
            print("%-20s  no search band" % name)
            continue
        k = int(np.argmax(mag[band]))
        rate = float(freqs[band][k])
        span = t[-1] - t[0]
        cycles = rate * span
        # is the peak actually a peak, or just the biggest bin of noise?
        m = mag[band]
        rest = np.delete(m, k)
        ratio = float(m[k] / (np.median(rest) + 1e-30))
        verdict = ("periodic, %.0fx median" % ratio) if ratio > 6 and cycles >= 2 else \
                  ("NOT RESOLVED (%.1f cycles in window, peak %.1fx median)" % (cycles, ratio))
        print("%-20s %10.1f %10.2f %10.3f %9.1f %s" %
              (name, mean, dev, rate, cycles, verdict))
    return 0


if __name__ == "__main__":
    sys.exit(main())
