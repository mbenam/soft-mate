#!/usr/bin/env python3
"""LFO rate as the period at which the SPECTRUM repeats.

For captures_backlog A2. This is the third estimator tried on this problem and
the only one that does not need a model of what the effect does internally.

Why not the other two, both recorded so nobody retries them blind:

* **Instantaneous frequency** (`lfo_pitch.py`) assumes the wet signal is one
  partial being swept -- a vibrato. The M8's chorus at 100% wet is not: its
  output carries several components at once, and the analytic signal locks onto
  whichever dominates. Measured on a real capture, the track sat at exactly
  523.25 Hz, then exactly 511.96, then exactly 534.53 -- flat plateaus at
  discrete offsets, which is a tracker hopping between sidebands, not a pitch
  being swept. `hw_findings.md` §UI-33 hit the same failure with a peak tracker
  on a different source.
* **Sideband spacing** assumes a clean `f0 +- n*f_LFO` comb. The comb measured
  here is dense and interleaved at ~1 Hz, not a single-rate set.

What survives both objections is that WHATEVER the effect is doing, it is driven
by a periodic LFO, so the whole short-time spectrum must repeat at the LFO
period. So: take a spectrogram, normalise each frame (the LFO changes the
spectral SHAPE, and normalising stops a slow amplitude drift from dominating),
and correlate the frame sequence against itself at every lag. The first strong
maximum is the period.

It reports the correlation at the chosen lag and refuses when that is weak, when
the window holds too few periods, or when the peak is not clearly a peak.
"""
from __future__ import annotations

import argparse
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
    return d.reshape(-1, ch).mean(axis=1), sr


def spectrogram(x, sr, hop_ms, win_ms, lo, hi):
    hop = max(1, int(sr * hop_ms / 1000.0))
    win = max(hop, int(sr * win_ms / 1000.0))
    W = np.hanning(win)
    nf = 1 + (len(x) - win) // hop
    if nf < 8:
        return None, hop / sr
    f = np.fft.rfftfreq(win, 1.0 / sr)
    band = (f >= lo) & (f <= hi)
    out = np.empty((nf, int(band.sum())))
    for i in range(nf):
        seg = x[i * hop:i * hop + win] * W
        out[i] = np.abs(np.fft.rfft(seg))[band]
    return out, hop / sr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav", nargs="+")
    ap.add_argument("--skip", type=float, default=0.6)
    ap.add_argument("--span", type=float, default=22.0)
    ap.add_argument("--hop-ms", type=float, default=4.0)
    ap.add_argument("--win-ms", type=float, default=40.0)
    ap.add_argument("--lo", type=float, default=380.0, help="spectrogram band, Hz")
    ap.add_argument("--hi", type=float, default=680.0)
    ap.add_argument("--min-period", type=float, default=0.02, help="s")
    ap.add_argument("--max-period", type=float, default=8.0, help="s")
    ap.add_argument("--min-periods", type=float, default=3.0,
                    help="how many LFO periods the window must hold")
    ap.add_argument("--min-r", type=float, default=0.30)
    ap.add_argument("--tol", type=float, default=0.90,
                    help="a lag counts as the period if its correlation is at least "
                         "this fraction of the best -- the smallest such lag wins. "
                         "Not 1.0: the true period rarely lands exactly on the lag "
                         "grid, so its peak reads slightly under an aligned MULTIPLE "
                         "of itself. At a true 7.50 Hz with a 10 ms hop the real peak "
                         "read 0.958 and the third multiple read 1.000.")
    ap.add_argument("--min-ratio", type=float, default=1.15,
                    help="chosen peak over the median of the correlation curve")
    a = ap.parse_args()

    print("%-16s %10s %10s %7s %7s %8s %s" %
          ("file", "period s", "rate Hz", "r", "pk/med", "periods", "verdict"))
    for path in a.wav:
        x, sr = read_mono(path)
        lo = int(a.skip * sr)
        x = x[lo:lo + int(a.span * sr)]
        name = path.replace("\\", "/").split("/")[-1]
        S, dt = spectrogram(x, sr, a.hop_ms, a.win_ms, a.lo, a.hi)
        if S is None:
            print("%-16s  too short" % name)
            continue

        # Normalise each frame to unit norm: the LFO changes the spectral SHAPE,
        # and this keeps a slow level drift from dominating the correlation.
        n = np.linalg.norm(S, axis=1, keepdims=True)
        n[n == 0] = 1.0
        S = S / n
        S = S - S.mean(axis=0, keepdims=True)

        lags = np.arange(int(a.min_period / dt), int(min(a.max_period, len(S) * dt / a.min_periods) / dt))
        lags = lags[(lags > 0) & (lags < len(S) - 8)]
        if len(lags) < 4:
            print("%-16s  no usable lag range" % name)
            continue
        r = np.empty(len(lags))
        for j, L in enumerate(lags):
            A = S[:len(S) - L]
            B = S[L:]
            da = np.linalg.norm(A)
            db = np.linalg.norm(B)
            r[j] = float(np.sum(A * B) / (da * db)) if da > 0 and db > 0 else 0.0

        # Skip the trivial short-lag correlation, THEN take the smallest lag that
        # is essentially as good as the best.
        #
        # Two ways to get this wrong, both hit on synthetic ground truth:
        #
        #   argmax alone picks an arbitrary MULTIPLE of the period, because a
        #   periodic signal correlates just as well at every one -- at a true
        #   1.00 Hz it returned 4.0000 s with r = 1.000 at both lags.
        #
        #   "smallest lag within tolerance" alone picks the shortest lag of all,
        #   because neighbouring spectrogram frames overlap and are trivially
        #   similar -- at a true 0.35 Hz it returned 0.0200 s, the minimum lag.
        #
        # So walk past the first dip (where the correlation falls well below its
        # own maximum) and only then look for the earliest strong peak.
        # The dip is found as the first LOCAL MINIMUM of a lightly smoothed curve,
        # not by a fixed threshold. A threshold of "below 0.4 of the maximum" was
        # tried and fails for a fast LFO, where the correlation never falls that
        # far between neighbouring peaks: at a true 7.50 Hz it sailed past the
        # first two peaks and returned 2.5000 Hz, exactly a third.
        rmax = float(r.max())
        w = max(3, int(0.005 / dt) | 1)
        rs = np.convolve(r, np.ones(w) / w, mode="same")
        j0 = 0
        for j in range(1, len(rs) - 1):
            if rs[j] <= rs[j - 1] and rs[j] < rs[j + 1]:
                j0 = j
                break
        tail = r[j0:]
        if len(tail) < 3:
            print("%-16s  no lag range past the trivial correlation" % name)
            continue
        tmax = float(tail.max())
        ok = np.flatnonzero(tail >= tmax * a.tol)
        k = j0 + (int(ok[0]) if len(ok) else int(np.argmax(tail)))
        while k + 1 < len(r) and r[k + 1] > r[k]:
            k += 1          # climb to the top of that peak
        # parabolic refine on the lag axis
        per = lags[k] * dt
        if 0 < k < len(r) - 1:
            A_, B_, C_ = r[k - 1], r[k], r[k + 1]
            den = A_ - 2 * B_ + C_
            if den != 0:
                per = (lags[k] + 0.5 * (A_ - C_) / den) * dt
        med = float(np.median(np.abs(r)))
        ratio = float(r[k] / med) if med > 0 else float("inf")
        periods = (len(S) * dt) / per if per > 0 else 0.0

        why = []
        if r[k] < a.min_r:
            why.append("correlation %.2f is weak" % r[k])
        if ratio < a.min_ratio:
            why.append("peak is %.2fx the median, not a peak" % ratio)
        if periods < a.min_periods:
            why.append("only %.1f periods in the window" % periods)
        verdict = "ok" if not why else "NOT RESOLVED: " + "; ".join(why)
        print("%-16s %10.4f %10.4f %7.3f %7.2f %8.1f %s" %
              (name, per, 1.0 / per if per else 0.0, r[k], ratio, periods, verdict))
    return 0


if __name__ == "__main__":
    sys.exit(main())
