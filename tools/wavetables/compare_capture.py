#!/usr/bin/env python3
"""Compare a real-M8 capture against the wave table frames extracted from the manual.

This is the tool that verified the extraction (WAVSYNTH_PHASE3_SPEC.md §7). Use
it to check any (shape, scan) pair against hardware, or to re-verify after a
change to the extractor.

Capture first, with the device set to the wave table under test and everything
else neutral (SIZE FF, MULT 00, WARP 00, FILTER 00, AMP 00, LIM 00, DRY C0,
sends 00):

    build/Release/m8_capture.exe --port COM3 --audio "M8" \
        --keyjazz 36 --seconds 2 --out cap.wav

Then:

    python tools/wavetables/compare_capture.py cap.wav OSC:GRAPHIC --scan 0x00

A low note (MIDI 36 = 65.4 Hz => ~734 samples per cycle at 48 kHz) is used so
one cycle comfortably oversamples the 200-sample frame.
"""
import argparse
import sys
import wave

import numpy as np

NX = 200
NROW = 64


def load_bank(path):
    blob = open(path, "rb").read()
    if blob[:4] != b"M8WT":
        sys.exit(f"{path}: not an M8WT bank")
    _ver, nt, nf, nx = blob[4], blob[5], blob[6], blob[7]
    off = 8
    names = [blob[off + 16 * i: off + 16 * (i + 1)].rstrip(b"\0").decode()
             for i in range(nt)]
    off += 16 * nt
    data = np.frombuffer(blob, dtype=np.int8, count=nt * nf * nx, offset=off)
    return names, data.reshape(nt, nf, nx).astype(np.float64) / 127.0


def cycle_from_wav(path, midi=36):
    """Phase-coherent average of every whole cycle, resampled to NX points."""
    w = wave.open(path)
    sr, ch = w.getframerate(), w.getnchannels()
    a = (np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
         .astype(np.float64) / 32768.0).reshape(-1, ch).mean(axis=1)
    seg = a[int(0.3 * sr):int(1.5 * sr)]
    period = sr / (440.0 * 2 ** ((midi - 69) / 12.0))
    ncyc = int((len(seg) - 4) / period) - 1
    if ncyc < 4:
        sys.exit("capture too short")
    frac = np.arange(NX) / NX
    acc = np.zeros(NX)
    for k in range(ncyc):
        t = k * period + frac * period
        i0 = np.floor(t).astype(int)
        f = t - i0
        acc += seg[i0] * (1 - f) + seg[i0 + 1] * f
    return acc / ncyc, ncyc


def norm(v):
    v = v - v.mean()
    m = np.abs(v).max()
    return v / m if m > 1e-12 else v


def circ_corr(a, b):
    """Best correlation over all circular shifts; the phase origin is arbitrary."""
    cc = np.fft.irfft(np.fft.rfft(a) * np.conj(np.fft.rfft(b)), n=len(a))
    den = np.linalg.norm(a) * np.linalg.norm(b)
    if den < 1e-12:
        return 0.0
    k = int(np.argmax(np.abs(cc)))
    return float(cc[k] / den)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("table", help='e.g. "OSC:GRAPHIC"')
    ap.add_argument("--bank", default="wavetables.bin")
    ap.add_argument("--scan", default=None, help="expected SCAN byte, e.g. 0x80")
    ap.add_argument("--midi", type=int, default=36)
    args = ap.parse_args()

    names, bank = load_bank(args.bank)
    if args.table not in names:
        sys.exit(f"unknown table {args.table!r}; have {names[:4]} ...")
    T = bank[names.index(args.table)]

    cyc, ncyc = cycle_from_wav(args.wav, args.midi)
    c = norm(cyc)
    ranked = sorted(((abs(circ_corr(c, norm(T[r]))), r) for r in range(NROW)),
                    reverse=True)
    print(f"{args.wav}: {ncyc} cycles averaged, "
          f"cycle peak {np.abs(cyc - cyc.mean()).max():.4f}")
    print("  best-matching frames:",
          [(round(v, 3), r) for v, r in ranked[:5]])

    if args.scan is not None:
        scan = int(args.scan, 0)
        pos = scan * (NROW - 1) / 255.0
        f0 = int(pos); f1 = min(f0 + 1, NROW - 1); mix = pos - f0
        blend = T[f0] * (1 - mix) + T[f1] * mix
        print(f"  SCAN 0x{scan:02X} -> frame {pos:.2f} "
              f"({f0} x {1-mix:.2f} + {f1} x {mix:.2f})")
        print(f"    vs crossfade  : {abs(circ_corr(c, norm(blend))):.4f}")
        print(f"    vs frame {f0:<2}   : {abs(circ_corr(c, norm(T[f0]))):.4f}")
        print(f"    vs frame {f1:<2}   : {abs(circ_corr(c, norm(T[f1]))):.4f}")


if __name__ == "__main__":
    main()
