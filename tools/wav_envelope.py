#!/usr/bin/env python3
"""Print a one-line amplitude envelope for a capture, so you can see the note.

Every summary metric hides the thing that actually goes wrong with a hardware
capture: the note not being in the window. `peak` alone cannot tell "the device
attenuated the signal by 23 dB" from "we recorded the silence between notes" --
and on 2026-08-19 that is exactly the mistake that got published, from
hwtest_out/fit/PFF.wav, whose every bin is below 0.01.

    python tools/wav_envelope.py hwtest_out/triage/*.wav

Each character is one time bin: '.' is below 0.01, otherwise a hex digit of the
bin's peak (F = full scale). A good capture of a probe note looks like

    0..................5550.......      note present, ~0.3 s, mid-window

and a bad one looks like

    ..............................      nothing was playing
"""
from __future__ import annotations

import struct
import sys
import wave


def envelope(path, nbins=30):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        if w.getsampwidth() != 2:
            raise ValueError("expected 16-bit PCM")
        s = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    # Left channel only: this is a presence check, not a stereo measurement.
    mono = [abs(s[i * ch]) / 32768.0 for i in range(n)]
    per = max(1, n // nbins)
    bins = [max(mono[b * per:(b + 1) * per] or [0.0]) for b in range(nbins)]
    return sr, n / float(sr), bins


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    worst = 0
    for path in argv:
        try:
            _, dur, bins = envelope(path)
        except Exception as e:                      # noqa: BLE001 - report and continue
            print("%-44s  unreadable: %s" % (path, e))
            worst = 1
            continue
        bar = "".join("." if v < 0.01 else "%X" % min(15, int(v * 16)) for v in bins)
        loud = sum(1 for v in bins if v >= 0.01)
        flag = "  <-- NO NOTE" if max(bins) < 0.05 else ""
        print("%-44s %5.2fs  %s  max %.4f  %2d/%d bins%s"
              % (path.replace("\\", "/").split("/")[-1], dur, bar, max(bins),
                 loud, len(bins), flag))
    return worst


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
