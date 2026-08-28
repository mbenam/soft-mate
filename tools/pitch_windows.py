"""Fundamental pitch per fixed window of a capture.

For sustaining patches, where onset counting cannot work because the audio never
falls silent between triggers. A phrase looping every N seconds gives one window
per loop, so a pitch that moves loop to loop is visible as a column of different
numbers.

Autocorrelation over a window, restricted to a musical range. Good to a few cents
on a periodic waveform, which is all that is needed to tell C-4 from anything
else.

    python tools/pitch_windows.py hwtest_out/rnlF0.wav --window 2.0
"""
import argparse
import struct
import sys
import wave


def read_mono(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            sys.exit(f"{path}: expected 16-bit")
        ch, sr, n = w.getnchannels(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    s = struct.unpack("<%dh" % (len(raw) // 2), raw)
    if ch == 1:
        return list(s), sr
    return [(s[i * ch] + s[i * ch + 1]) * 0.5 for i in range(len(s) // ch)], sr


def fundamental(buf, sr, lo=40.0, hi=2000.0):
    """Autocorrelation peak, ignoring lag 0. Returns Hz, or 0 if too quiet."""
    n = len(buf)
    peak = max((abs(v) for v in buf), default=0.0)
    if n < 512 or peak < 200:            # ~0.006 full scale on int16
        return 0.0

    mean = sum(buf) / n
    x = [v - mean for v in buf]

    lag_min = max(2, int(sr / hi))
    lag_max = min(n - 1, int(sr / lo))

    best_lag, best = 0, 0.0
    for lag in range(lag_min, lag_max):
        acc = 0.0
        # stride the correlation: full precision is not needed to pick a peak
        for i in range(0, n - lag, 4):
            acc += x[i] * x[i + lag]
        if acc > best:
            best, best_lag = acc, lag
    return sr / best_lag if best_lag else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav", nargs="+")
    ap.add_argument("--window", type=float, default=2.0,
                    help="seconds per window; set it to the phrase loop length")
    ap.add_argument("--skip", type=float, default=0.15,
                    help="seconds skipped at each window start, past the attack")
    a = ap.parse_args()

    for path in a.wav:
        buf, sr = read_mono(path)
        win = int(a.window * sr)
        skip = int(a.skip * sr)
        # one second of analysis per window is plenty and keeps this quick
        span = min(win - skip, sr)
        print(f"{path}  ({len(buf)/sr:.2f}s, window {a.window}s)")
        for i, start in enumerate(range(0, len(buf) - win, win)):
            seg = buf[start + skip: start + skip + span]
            hz = fundamental(seg, sr)
            if hz <= 0:
                print(f"  win {i:2d}  (silent)")
            else:
                import math
                midi = 69 + 12 * math.log2(hz / 440.0)
                print(f"  win {i:2d}  {hz:8.2f} Hz   midi {midi:6.2f}")


if __name__ == "__main__":
    main()
