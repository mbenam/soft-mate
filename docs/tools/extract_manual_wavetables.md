# extract_manual_wavetables

**Source:** [`tools/wavetables/extract_manual_wavetables.py`](../../tools/wavetables/extract_manual_wavetables.py) (Python 3)
**Build target:** none — a Python script. Needs `pymupdf` and `numpy`.
**Category:** data extraction. Digitises the 61 WavSynth wave tables out of the
manual's Wave Table Index and emits them as the engine's wave table bank.
**Links:** nothing. Reads a PDF, writes a `.cpp` (and optionally a `.bin`/`.npz`).
**Spec:** [`WAVSYNTH_PHASE3_SPEC.md`](../../specs/WAVSYNTH_PHASE3_SPEC.md)

## What it does

`manual/wavesynth.pdf` contains the M8 manual's *Wave Table Index* — eight pages
plotting all 61 built-in wave tables, each as a column of 64 frames. Those plots
are **vector polylines, not raster images**. That makes the index a
machine-readable source for the wave table data itself, not merely a picture of
it: this script recovers the polylines and writes out the sample values.

A capture from real hardware matches the digitised frame at **r = 0.998**
(`WAVSYNTH_PHASE3_SPEC.md` §7.1), so the output is the wave table data, not an
impression of it.

Default output is `src/engine/data/WavetableBank.cpp` — a **generated C++
translation unit**, not a data file. The engine must not depend on the working
directory (`m8_tests`, `m8_render` and `m8_clone` all run from different places,
and `m8_engine` links no file-system helpers), so the bank is compiled in. It
costs 3.7s to compile with MSVC `/O2 /std:c++20` and produces a 791KB object,
measured before that choice was made.

## What it recovers, and why nothing is hardcoded

Every geometric constant is read from the document. The ones that matter:

| Quantity | Value | How it is obtained |
|---|---|---|
| Tables | 61, 8 per page (5 on page 7) | Column header text spans (`09:OSC:CRUSH`, …) |
| Frames per table | 64 | Hex row labels down the left of each column |
| Samples per frame | **200** | Distinct x lattice positions per plot (~0.141pt pitch) |
| Row pitch | 7.6089pt (p0), 7.6818pt (p1–6), 7.6855pt (p7) | Mean spacing of that page's row labels |
| Row origin | ~77.6–79.0pt, per page | First row label's bbox top |
| Vertical baseline | ~+2.484pt from row top | Midpoint of the page's full stroke envelope |
| Full-scale half-span | ~3.487pt on every page | Half the page's full stroke envelope |

The half-span landing on ~3.487pt on all eight pages independently is the
cross-check that the pages share one vertical scale — it is not assumed, and if a
future manual revision changes it the number will visibly stop agreeing.

**200 samples per frame is a ceiling, not a choice.** The device's own buffer is
finer — the plots change value at nearly every one of the 200 x positions, so no
coarser source lattice is visible. The bank stores exactly what the manual can
give and invents no precision beyond it.

## CLI flags

| Flag | Default | Meaning |
|---|---|---|
| `--pdf <path>` | `manual/wavesynth.pdf` | The manual page set to read. |
| `--out <path>` | `src/engine/data/WavetableBank.cpp` | Generated C++ bank. This is what the engine builds. |
| `--bin <path>` | *(off)* | Also write a flat binary bank, for the verification tooling. |
| `--npz <path>` | *(off)* | Also write the float arrays, for ad-hoc analysis in Python. |

Progress goes to **stderr** (one line per page, with that page's recovered
geometry); the summary goes to stdout. Exit code is 0 on success; any structural
surprise (wrong table count, wrong stroke count in a column, wrong number of row
labels) raises rather than writing a partial bank.

## Output formats

**C++** (`--out`) — `kWavetableNames[61]` and
`kWavetableData[61][64][200]` as `int8_t`, `-127..127` mapping to `-1.0..+1.0`.
Declared by `src/engine/data/WavetableBank.h`, which is **hand-written, not
generated** — if you change the array shape here, change that header too.

**Binary** (`--bin`) — consumed by
[`compare_capture`](compare_capture.md):

```
magic    "M8WT"                       4 bytes
version  1                            uint8
tables   61                           uint8
frames   64                           uint8
length   200                          uint8
names    61 x 16 bytes, NUL-padded    "OSC:CRUSH", ...
data     61 * 64 * 200 int8           row-major [table][frame][sample]
```

## Gotchas

- **`manual/` is gitignored** (`.gitignore:16`). A clean clone has no PDF and
  **cannot re-run this tool**. That is precisely why the generated
  `WavetableBank.cpp` is committed rather than built. Do not delete it expecting
  to regenerate it, and do not add a build step that tries to.
- **Row pitch differs per page and must not be shared.** Page 0 uses 7.6089pt,
  the rest 7.6818/7.6855pt. Applying one pitch to the whole document accumulates
  to roughly half a row by frame 63, which silently mis-assigns the bottom of
  every column on seven of the eight pages. This was a real bug during
  development, and it looks like plausible data rather than an error.
- **Rows are assigned by rank, not by rounding the stroke's centre.** A
  full-amplitude frame's bounding-box centre can drift far enough to round into
  its neighbour's row. Every column has exactly 64 strokes, so sorting by centre
  and taking ranks 0–63 is exact where rounding is not. Nine frames were
  mis-assigned before this change.
- **Bank index `i` is WavSynth shape `0x09 + i`.** Index 0 is `OSC:CRUSH`
  (shape `0x09`); index 60 is `VOX:VOXSYNTH` (shape `0x45`). The 61 PDF header
  names match, in order, the 61 names read off a real device screen — 61/61 — so
  a reordering here would break that agreement and `[wavsynth]` asserts it.
- **The library enum spells one name differently.** `m8::WavShape` has
  `WtAsimmitry` for `0x22`; the device and the manual both draw `ASYMMTRY`. The
  bank uses the manual's spelling. Cosmetic, but do not "fix" one to match the
  other.
- **Frames keep their DC offset.** They are stored exactly as plotted; 29% have a
  mean beyond ±0.05 of full scale and a few are nearly pure DC. That is faithful
  to the source and no measurement has contradicted it, but nothing has confirmed
  the device does not remove DC downstream either (`WAVSYNTH_PHASE3_SPEC.md` P3).
- **Vertical segments in a polyline are jumps, not data.** Where consecutive
  samples differ by more than one lattice step the plot draws a vertical
  connector; the sampler averages its endpoints, which is the best estimate of
  the value at that x. Taking either endpoint instead biases every steep edge.

## Example

```bash
python tools/wavetables/extract_manual_wavetables.py --bin wavetables.bin
```

Then verify against hardware with [`compare_capture`](compare_capture.md).
