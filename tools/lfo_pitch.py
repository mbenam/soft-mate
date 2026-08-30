#!/usr/bin/env python3
"""LFO rate from the vibrato of a 100%-wet chorus, by instantaneous frequency.

For captures_backlog A2. A chorus at full wet is a modulated delay with no dry
path, which is a vibrato: the wet output's instantaneous frequency is
f0 * (1 - dtau/dt), so it wobbles once per LFO cycle. Reading the rate is then
"how fast does the pitch wobble", with no harmonic ambiguity -- unlike the
amplitude route, where the comb can put several extrema in one LFO period.

**This needs a spectrally clean carrier and says so when it does not have one.**
`hw_findings.md` §UI-33 failed here on a WAVSYNTH probe carrying inharmonic
partials 27-34 dB down: a peak tracker hopped between them and reported a
±1264 Hz swing, more than an octave, which no chorus does. The §UI-30 FM patch
(ALGO 0B, operator A alone on a sine) is clean enough -- its 3rd harmonic is
30.5 dB down and its 5th 48 dB down, both far outside the analysis band.

Method, in order:

1. **Narrow bandpass around the carrier, in the FFT domain.** The band is set
   from the measured carrier and a half-width wide enough for the deviation but
   narrow enough to exclude the 3rd harmonic. Everything after this sees one
   partial, which is what makes the analytic signal meaningful.
2. **Analytic signal -> unwrapped phase -> derivative** = instantaneous
   frequency, in Hz, sample by sample.
3. **Trim the edges**, where the FFT bandpass rings.
4. **FFT of the instantaneous-frequency track** -> the LFO rate.

It refuses rather than guessing. `NOT RESOLVED` is printed, with the reason, when
the window holds fewer than `--min-cycles` LFO periods, when the spectral peak is
not clearly a peak against the median, or when the deviation is a larger fraction
of the carrier than a chorus can produce -- the signature of a tracker that has
lost the carrier.
"""
from __future__ import annotations

import argparse
import math
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
    st = d.reshape(-1, ch)
    return st.mean(axis=1), sr


def carrier_of(x, sr, lo=80.0, hi=8000.0):
    W = np.hanning(len(x))
    S = np.abs(np.fft.rfft(x * W))
    f = np.fft.rfftfreq(len(x), 1.0 / sr)
    band = (f >= lo) & (f <= hi)
    k = int(np.flatnonzero(band)[np.argmax(S[band])])
    # parabolic interpolation on the log magnitude
    if 0 < k < len(S) - 1:
        a, b, c = (20 * np.log10(S[k - 1] + 1e-30), 20 * np.log10(S[k] + 1e-30),
                   20 * np.log10(S[k + 1] + 1e-30))
        d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
        return float(f[k] + d * (f[1] - f[0]))
    return float(f[k])


def analytic_bandpassed(x, sr, centre, half):
    """Analytic signal of x restricted to [centre-half, centre+half]."""
    n = len(x)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    keep = (f >= centre - half) & (f <= centre + half)
    Y = np.zeros(len(X), dtype=complex)
    Y[keep] = X[keep]
    # one-sided spectrum doubled = analytic signal (DC and Nyquist not doubled,
    # and neither is in the band)
    return np.fft.irfft(Y, n) + 1j * np.imag(_hilbert_from_rfft(Y, n))


def _hilbert_from_rfft(Y, n):
    full = np.zeros(n, dtype=complex)
    full[:len(Y)] = Y
    full[0] = 0
    if n % 2 == 0 and len(Y) > 1:
        full[len(Y) - 1] = full[len(Y) - 1]
    z = np.fft.ifft(full * 2.0)
    return z


def inst_freq(x, sr, centre, half, env_floor=0.3):
    """Instantaneous frequency, with the envelope-dip glitches repaired.

    The phase derivative is meaningless wherever the analytic signal's magnitude
    approaches zero: the angle swings arbitrarily and one sample can read tens of
    kHz. Measured on a real capture, an otherwise clean track ran from -20035 to
    +22881 Hz on a 523 Hz carrier because of those instants alone, and the
    spikes -- being impulses -- spread flat across the rate spectrum and buried
    the LFO peak under its own median (peak-to-median 2.7, refused).

    So samples whose envelope falls below `env_floor` of the median are dropped
    and linearly interpolated across. This repairs an artefact of the estimator,
    not of the signal: it cannot invent a periodicity, and if too much is dropped
    the caller is told rather than handed a number.
    """
    n = len(x)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    keep = (f >= centre - half) & (f <= centre + half)
    Y = np.zeros(n, dtype=complex)
    Y[:len(X)] = np.where(keep, X, 0.0)
    Y[1:len(X)] *= 2.0          # analytic: double the positive frequencies
    z = np.fft.ifft(Y)
    ph = np.unwrap(np.angle(z))
    fi = np.diff(ph) * sr / (2 * math.pi)
    env = np.abs(z)[1:]
    good = env > env_floor * np.median(env)
    if good.any() and not good.all():
        idx = np.arange(len(fi))
        fi = np.interp(idx, idx[good], fi[good])
    return fi, env, float(np.count_nonzero(~good)) / max(len(fi), 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav", nargs="+")
    ap.add_argument("--skip", type=float, default=0.5, help="seconds to skip at the start")
    ap.add_argument("--span", type=float, default=6.0, help="seconds to analyse")
    ap.add_argument("--half", type=float, default=180.0,
                    help="bandpass half-width in Hz around the carrier")
    ap.add_argument("--lo", type=float, default=0.05, help="lowest LFO rate to report")
    ap.add_argument("--hi", type=float, default=60.0, help="highest LFO rate to report")
    ap.add_argument("--min-cycles", type=float, default=2.0)
    ap.add_argument("--min-ratio", type=float, default=4.0,
                    help="peak-to-median of the rate spectrum below which it is not a peak")
    a = ap.parse_args()

    print("%-22s %10s %9s %9s %8s %8s %7s %s" %
          ("file", "carrier", "dev +-Hz", "rate Hz", "cycles", "pk/med", "dropped", "verdict"))
    for path in a.wav:
        x, sr = read_mono(path)
        lo = int(a.skip * sr)
        hi = min(len(x), lo + int(a.span * sr))
        seg = x[lo:hi]
        name = path.replace("\\", "/").split("/")[-1]
        if len(seg) < sr:
            print("%-22s  too short (%.2f s)" % (name, len(seg) / sr))
            continue

        centre = carrier_of(seg, sr)
        fi, env, dropped = inst_freq(seg, sr, centre, a.half)
        # trim the FFT bandpass's edge ringing
        edge = int(0.15 * sr)
        fi = fi[edge:-edge]
        if len(fi) < sr:
            print("%-22s  window too short after trimming" % name)
            continue

        dev = float(np.percentile(np.abs(fi - np.median(fi)), 99))
        t = fi - np.mean(fi)
        W = np.hanning(len(t))
        S = np.abs(np.fft.rfft(t * W))
        f = np.fft.rfftfreq(len(t), 1.0 / sr)
        band = (f >= a.lo) & (f <= a.hi)
        if not band.any():
            print("%-22s  no LFO band" % name)
            continue
        idx = np.flatnonzero(band)
        k = int(idx[np.argmax(S[idx])])
        # parabolic refine
        rate = float(f[k])
        if 0 < k < len(S) - 1:
            A, B, C = S[k - 1], S[k], S[k + 1]
            den = (A - 2 * B + C)
            if den != 0:
                rate = float(f[k] + 0.5 * (A - C) / den * (f[1] - f[0]))
        med = float(np.median(S[idx]))
        ratio = float(S[k] / med) if med > 0 else float("inf")
        cycles = rate * len(t) / sr

        why = []
        if cycles < a.min_cycles:
            why.append("only %.1f cycles in the window" % cycles)
        if ratio < a.min_ratio:
            why.append("peak is %.1fx the median, not a peak" % ratio)
        if dev > 0.5 * centre:
            why.append("deviation %.0f Hz is %.0f%% of the carrier -- lost it"
                       % (dev, 100 * dev / centre))
        if dropped > 0.25:
            why.append("%.0f%% of samples dropped as envelope dips" % (100 * dropped))
        verdict = "ok" if not why else "NOT RESOLVED: " + "; ".join(why)
        print("%-22s %10.2f %9.2f %9.4f %8.1f %8.1f %6.1f%% %s" %
              (name, centre, dev, rate, cycles, ratio, 100 * dropped, verdict))
    return 0


if __name__ == "__main__":
    sys.exit(main())
