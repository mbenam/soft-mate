#!/usr/bin/env python3
"""Digitise the 61 WavSynth wave tables out of the manual's Wave Table Index.

`manual/wavesynth.pdf` draws every one of the 61 built-in wave tables as a
column of 64 frames, and each frame is a **vector polyline**, not a picture.
That makes the index a machine-readable source for the wave table data: this
script recovers the polylines and writes them out as the engine's wave table bank.

    python tools/wavetables/extract_manual_wavetables.py \
        --pdf manual/wavesynth.pdf --out src/engine/data/WavetableBank.cpp

It writes the bank as a generated C++ translation unit -- 61 tables x 64 frames
x 200 int8 samples -- because the engine must not depend on a data file at
runtime. `--bin` additionally writes the same data as a flat binary for the
verification tooling (see WAVSYNTH_PHASE3_SPEC.md section 7).

What the geometry means, and why each constant is read rather than assumed:

* Frames sit on a per-page row lattice. Page 0 uses a 7.6089pt pitch, pages 1-6
  use 7.6818pt and page 7 uses 7.6855pt, so the pitch and origin are recovered
  from the hex row labels on each page. Assuming one pitch for the whole
  document drifts by half a row by frame 63.
* The vertical baseline and full-scale half-span are recovered per page from the
  envelope of every stroke on it. Many frames hit the plot's full scale, so the
  envelope pins the scale exactly; it comes out at ~3.487pt on every page, which
  is the cross-check that the pages share one vertical scale.
* Each frame is plotted at exactly 200 x positions on a ~0.141pt lattice. That
  is the resolution ceiling of this source: the underlying buffer is finer (the
  plots change value at nearly every x position), so 200 samples per frame is
  what the manual can give and the bank stores exactly that -- no invented
  precision.
"""
import argparse
import sys
from collections import defaultdict

import numpy as np

try:
    import fitz  # PyMuPDF
except ImportError:
    sys.exit("PyMuPDF is required:  pip install pymupdf")

NX = 200        # x positions per frame -- the manual's plot resolution
NROW = 64       # frames per table
NTABLE = 61


def page_headers(page):
    """[(x0, 'NN:CAT:NAME')] for one page, left to right."""
    out = []
    for block in page.get_text("dict")["blocks"]:
        for line in block.get("lines", []):
            for span in line["spans"]:
                if ":" in span["text"] and span["size"] < 5:
                    out.append((span["bbox"][0], span["text"]))
    out.sort()
    return out


def page_rows(page):
    """(top_of_row_0, row_pitch) from the hex row labels of the left column."""
    ys = []
    for block in page.get_text("dict")["blocks"]:
        for line in block.get("lines", []):
            for span in line["spans"]:
                if span["size"] < 3 and len(span["text"]) == 2 and span["bbox"][0] < 50:
                    ys.append(span["bbox"][1])
    ys.sort()
    if len(ys) != NROW:
        raise RuntimeError(f"expected {NROW} row labels, found {len(ys)}")
    return ys[0], float(np.mean(np.diff(ys)))


def sample_polyline(items, xs):
    """Sample a continuous, x-monotonic polyline at each x in xs.

    The plots contain vertical segments wherever consecutive samples differ by
    more than one lattice step. At such an x the midpoint of the jump is the
    best estimate of the underlying value, so both endpoints are averaged in.
    """
    acc = np.zeros(len(xs))
    cnt = np.zeros(len(xs))
    x0g, dx = xs[0], xs[1] - xs[0]
    for item in items:
        if item[0] != "l":
            continue
        a, b = item[1], item[2]
        ax, ay, bx, by = a.x, a.y, b.x, b.y
        if bx < ax:
            ax, ay, bx, by = bx, by, ax, ay
        i0 = max(0, int(np.floor((ax - x0g) / dx)))
        i1 = min(len(xs) - 1, int(np.ceil((bx - x0g) / dx)))
        for i in range(i0, i1 + 1):
            x = xs[i]
            if x < ax - 1e-6 or x > bx + 1e-6:
                continue
            if bx - ax < 1e-9:
                acc[i] += (ay + by) * 0.5
            else:
                acc[i] += ay + (by - ay) * (x - ax) / (bx - ax)
            cnt[i] += 1
    out = np.where(cnt > 0, acc / np.maximum(cnt, 1), np.nan)
    idx = np.arange(len(xs))
    good = ~np.isnan(out)
    if not good.all():                      # a gap can only come from rounding
        out = np.interp(idx, idx[good], out[good])
    return out


def extract(pdf_path, verbose=True):
    """-> [(name, (64, NX) float array in -1..+1)] in wave-table index order."""
    doc = fitz.open(pdf_path)
    tables = []
    for pi, page in enumerate(doc):
        headers = page_headers(page)
        top0, pitch = page_rows(page)
        strokes = [g for g in page.get_drawings()
                   if g["type"] == "s" and g["rect"].y0 > 70]
        colx = [h[0] for h in headers]
        buckets = defaultdict(list)
        for g in strokes:
            ci = int(np.argmin([abs(g["rect"].x0 - (cx + 4.2)) for cx in colx]))
            buckets[ci].append(g)

        lo, hi = [], []
        for ci in buckets:
            gs = sorted(buckets[ci], key=lambda z: (z["rect"].y0 + z["rect"].y1) / 2)
            for r, g in enumerate(gs[:NROW]):
                rt = top0 + pitch * r
                lo.append(g["rect"].y0 - rt)
                hi.append(g["rect"].y1 - rt)
        base = (min(lo) + max(hi)) / 2.0
        half = (max(hi) - min(lo)) / 2.0
        if verbose:
            print(f"page {pi}: {len(headers)} tables  pitch={pitch:.4f}pt  "
                  f"baseline=+{base:.4f}pt  half_scale={half:.4f}pt", file=sys.stderr)

        for ci, (_cx, name) in enumerate(headers):
            gs = sorted(buckets[ci], key=lambda z: (z["rect"].y0 + z["rect"].y1) / 2)
            if len(gs) != NROW:
                raise RuntimeError(f"{name}: {len(gs)} strokes, expected {NROW}")
            arr = np.zeros((NROW, NX))
            for r, g in enumerate(gs):
                x0, x1 = g["rect"].x0, g["rect"].x1
                if x1 - x0 > 1.0:
                    y = sample_polyline(g["items"], np.linspace(x0, x1, NX))
                else:                        # a frame drawn as a single flat run
                    y = np.full(NX, (g["rect"].y0 + g["rect"].y1) / 2)
                arr[r] = y - (top0 + pitch * r + base)
            tables.append((name, -arr / half))   # PDF y grows downward
    if len(tables) != NTABLE:
        raise RuntimeError(f"expected {NTABLE} tables, got {len(tables)}")
    return tables


def write_bank(tables, out_path):
    blob = bytearray(b"M8WT")
    blob += bytes([1, NTABLE, NROW, NX])
    for name, _ in tables:
        short = name.split(":", 1)[1]            # "09:OSC:CRUSH" -> "OSC:CRUSH"
        raw = short.encode("ascii")[:16]
        blob += raw + b"\x00" * (16 - len(raw))
    for _, arr in tables:
        q = np.clip(np.rint(arr * 127.0), -127, 127).astype(np.int8)
        blob += q.tobytes()
    with open(out_path, "wb") as f:
        f.write(blob)
    return len(blob)



def write_cpp(tables, out_path):
    """Emit the bank as a C++ translation unit.

    The engine must not depend on a data file at runtime: m8_tests, m8_render
    and m8_clone all run from different working directories, and the engine
    links no file-system helpers. So the bank ships as a generated .cpp.
    """
    import os
    NL = chr(10)
    names = [n.split(":", 1)[1] for n, _ in tables]
    with open(out_path, "w", encoding="utf-8", newline=NL) as f:
        w = f.write
        w("// GENERATED by tools/wavetables/extract_manual_wavetables.py" + NL)
        w("// Source: manual/wavesynth.pdf (Wave Table Index)." + NL)
        w("// Do not edit by hand -- re-run the extractor instead." + NL)
        w('#include "WavetableBank.h"' + NL + NL)
        w("namespace m8::engine {" + NL + NL)
        w("const char* const kWavetableNames[kWavetableCount] = {" + NL)
        for n in names:
            w('    "' + n + '",' + NL)
        w("};" + NL + NL)
        w("const int8_t kWavetableData[kWavetableCount]"
          "[kWavetableFrames][kWavetableLength] = {" + NL)
        for name, arr in tables:
            q = np.clip(np.rint(arr * 127.0), -127, 127).astype(np.int8)
            w("{ // " + name + NL)
            for r in range(NROW):
                w("{" + ",".join(str(int(v)) for v in q[r]) + "}," + NL)
            w("}," + NL)
        w("};" + NL + NL + "} // namespace m8::engine" + NL)
    return os.path.getsize(out_path)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pdf", default="manual/wavesynth.pdf")
    ap.add_argument("--out", default="src/engine/data/WavetableBank.cpp",
                    help="generated C++ bank (this is what the engine builds)")
    ap.add_argument("--bin", help="optional flat binary bank, for analysis tooling")
    ap.add_argument("--npz", help="also dump the float arrays for analysis")
    args = ap.parse_args()

    tables = extract(args.pdf)
    peak = max(float(np.abs(a).max()) for _, a in tables)
    n = write_cpp(tables, args.out)
    print(f"wrote {args.out}: {n} bytes, {NTABLE} tables x {NROW} frames x {NX} "
          f"samples, peak={peak:.3f}")
    if args.bin:
        print(f"wrote {args.bin}: {write_bank(tables, args.bin)} bytes")
    if args.npz:
        np.savez_compressed(args.npz,
                            names=np.array([t[0] for t in tables]),
                            **{t[0]: t[1] for t in tables})
        print("wrote", args.npz)


if __name__ == "__main__":
    main()
