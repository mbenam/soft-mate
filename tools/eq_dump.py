#!/usr/bin/env python3
"""Decode the EQ banks out of .m8s song files.

EQ_SPEC.md step 1. Two things in that spec are inferences, and this settles both
without touching hardware:

  4a. How the coarse/fine byte pairs combine into a frequency in Hz and a gain in
      dB. The hypothesis is a 16-bit big-endian value -- Hz directly for
      frequency, hundredths of a dB (signed) for gain. If that is right the
      numbers land in musical ranges; if it is wrong they will be obvious
      nonsense.
  4b. Whether ALLPASS is filter type 6. The file library's enum stops at 5 and
      clamps anything higher to Bell, so a band with type 6 in a real song would
      confirm the manual's seventh type exists.

Layout, from third_party/m8-files-cxx (song.cpp offsets, types.cpp readers):

  version   bytes 10..11: major = msb & 0x0F, minor = (lsb >> 4) & 0x0F
  eq banks  at 0x1AD5E, 18 bytes each (3 bands x 6 bytes)
  band      mode_type, freq_fin, freq, level_fin, level, q
  mode_type type = value & 0x7, mode = (value >> 5) & 0x7
  count     32 banks on V4, 128 on V4.1 -- version-dependent, do not hardcode

Usage:  python tools/eq_dump.py <file.m8s> [more.m8s ...]
"""

import sys
import struct

EQ_OFFSET = 0x1AD5A + 4
BAND_SIZE = 6
BANK_SIZE = 18

TYPES = ["LOWCUT", "LOWSHELF", "BELL", "BANDPASS", "HI.SHELF", "HI.CUT", "ALLPASS?"]
MODES = ["STEREO", "MID", "SIDE", "LEFT", "RIGHT"]


def read_version(data):
    lsb, msb = data[10], data[11]
    return (msb & 0x0F, (lsb >> 4) & 0x0F, lsb & 0x0F)


def decode_band(raw):
    mode_type, freq_fin, freq, level_fin, level, q = raw
    t = mode_type & 0x7
    m = (mode_type >> 5) & 0x7
    # Hypothesis 4a: 16-bit big-endian, coarse first.
    freq_hz = (freq << 8) | freq_fin
    gain_raw = (level << 8) | level_fin
    gain_signed = struct.unpack(">h", struct.pack(">H", gain_raw))[0]
    return {
        "type": t,
        "type_name": TYPES[t] if t < len(TYPES) else f"?{t}",
        "mode": m,
        "mode_name": MODES[m] if m < len(MODES) else f"?{m}",
        "freq_hz": freq_hz,
        "gain_db": gain_signed / 100.0,
        "q": q,
        "raw": tuple(raw),
    }


# .m8i instrument files carry a single EQ at the very end. V4_1_OFFSETS gives
# 0x165; the library only reads it when the file is long enough, because an
# instrument saved without an EQ simply stops short (357 bytes vs 375).
INSTRUMENT_EQ_OFFSET = 0x165


def dump_instrument(path, data):
    major, minor, patch = read_version(data)
    print(f"\n=== {path}")
    print(f"    version {major}.{minor}.{patch}  (size {len(data)} bytes)")
    end = INSTRUMENT_EQ_OFFSET + BANK_SIZE
    if len(data) < end:
        print(f"    no EQ block (needs {end} bytes, file has {len(data)})")
        return
    for name, i in (("LOW ", 0), ("MID ", 1), ("HIGH", 2)):
        off = INSTRUMENT_EQ_OFFSET + i * BAND_SIZE
        d = decode_band(data[off:off + BAND_SIZE])
        print(f"      {name} {d['type_name']:<9} {d['mode_name']:<7}"
              f" freq {d['freq_hz']:>6} Hz  gain {d['gain_db']:+7.2f} dB  Q {d['q']:>3}"
              f"   raw {' '.join('%02X' % x for x in d['raw'])}")


def dump(path):
    with open(path, "rb") as f:
        data = f.read()

    if path.lower().endswith(".m8i"):
        dump_instrument(path, data)
        return

    major, minor, patch = read_version(data)
    count = 128 if (major, minor) >= (4, 1) else 32
    print(f"\n=== {path}")
    print(f"    version {major}.{minor}.{patch}  ->  {count} banks  (size {len(data)} bytes)")

    if major < 4:
        print("    pre-4.0: no EQ block")
        return
    if len(data) < EQ_OFFSET + count * BANK_SIZE:
        print(f"    FILE TOO SHORT for {count} banks at 0x{EQ_OFFSET:X} -- offset or count is wrong")
        return

    freqs, gains, qs = [], [], []
    types_seen, modes_seen = {}, {}
    nondefault = []

    for b in range(count):
        base = EQ_OFFSET + b * BANK_SIZE
        bands = []
        for band in range(3):
            off = base + band * BAND_SIZE
            d = decode_band(data[off:off + BAND_SIZE])
            bands.append(d)
            freqs.append(d["freq_hz"])
            gains.append(d["gain_db"])
            qs.append(d["q"])
            types_seen[d["type"]] = types_seen.get(d["type"], 0) + 1
            modes_seen[d["mode"]] = modes_seen.get(d["mode"], 0) + 1
        if any(x["raw"] != (0, 0, 0, 0, 0, 0) for x in bands):
            nondefault.append((b, bands))

    print(f"    banks with any non-zero bytes: {len(nondefault)} of {count}")
    if freqs:
        print(f"    freq  min {min(freqs)} Hz   max {max(freqs)} Hz")
        print(f"    gain  min {min(gains):+.2f} dB  max {max(gains):+.2f} dB")
        print(f"    Q     min {min(qs)}        max {max(qs)}")
    print("    types seen: " + ", ".join(
        f"{TYPES[t] if t < len(TYPES) else '?'+str(t)}={n}" for t, n in sorted(types_seen.items())))
    print("    modes seen: " + ", ".join(
        f"{MODES[m] if m < len(MODES) else '?'+str(m)}={n}" for m, n in sorted(modes_seen.items())))

    # The four EQs the file library does not model -- main mix, then the three
    # send effects -- sit immediately after the bank array in the same format
    # (EQ_SPEC.md §4c). Located by saving one project twice on a real M8,
    # identical but for the mix EQ, and diffing.
    extra = EQ_OFFSET + count * BANK_SIZE
    names = ("MIX     ", "ModFX?  ", "Delay?  ", "Reverb? ")
    print(f"    --- unmodeled EQs at 0x{extra:X} ({len(data) - extra} bytes follow) ---")
    for k, label in enumerate(names):
        base = extra + k * BANK_SIZE
        if base + BANK_SIZE > len(data):
            print(f"    {label} (past end of file)")
            continue
        cells = []
        for i in range(3):
            d = decode_band(data[base + i * BAND_SIZE: base + (i + 1) * BAND_SIZE])
            cells.append(f"{d['type_name']:<9}{d['freq_hz']:>6}Hz {d['gain_db']:+6.2f}dB Q{d['q']:>3}")
        print(f"    {label} " + " | ".join(cells))

    for b, bands in nondefault[:4]:
        print(f"    bank {b:02X}:")
        for name, d in zip(("LOW ", "MID ", "HIGH"), bands):
            print(f"      {name} {d['type_name']:<9} {d['mode_name']:<7}"
                  f" freq {d['freq_hz']:>6} Hz  gain {d['gain_db']:+7.2f} dB  Q {d['q']:>3}"
                  f"   raw {' '.join('%02X' % x for x in d['raw'])}")
    if len(nondefault) > 4:
        print(f"    ... and {len(nondefault) - 4} more non-default banks")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    for p in sys.argv[1:]:
        dump(p)
