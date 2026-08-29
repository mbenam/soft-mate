#!/usr/bin/env python3
"""Guarded level captures: set a field, assert the rig, capture, assert again.

For captures_backlog A6 (LIM and OTT transfer curves) and A3 (send returns).
Same before-and-after discipline as `rt60_run.py`, but it holds **one**
`m8_nav --serve` session per assertion instead of spawning four processes, since
A6 needs dozens of captures and the screen reads are the expensive half.

**Keyjazz velocity is not the input-level control here, though the obvious
reading of `m8_capture`'s docs says it should be.** Measured on this rig: the
WAVSYNTH probe at velocity `0x40` and at `0x7F` produced peak 0.3478 both times,
identical to four decimals, and RMS within 0.06 dB. That instrument's volume
comes from its AHD envelope at amount `0xFF`, and nothing routes velocity to it.
So the sweep drives a named field instead, and every point costs a screen edit
and two assertions.

    python tools/level_run.py --screen MIXER --field TRACK1_VOL \\
        --values 20 40 60 80 A0 C0 E0 --tag limOFF

Exit 0 = every capture taken with the rig verified on both sides. 2 = a field
would not take, or the rig moved (that capture is renamed `.DRIFTED`). 3 = a
capture failed.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tools", "m8drv"))
from m8drv import M8Driver  # noqa: E402

CAPTURE = os.path.join(REPO, "build", "Release", "m8_capture.exe")


def snapshot(screens, set_field=None):
    """Decoded row text per screen, from one driver session.

    `set_field` is (screen, field, value) and is applied inside the same session,
    so a point costs one daemon start rather than two.
    """
    out = {}
    with M8Driver() as d:
        if set_field:
            scr, field, value = set_field
            d.goto(scr)
            d.set_field(field, value)
            got = d.read_field(field)
            if got is None or not got.upper().startswith(value.upper()):
                raise SystemExit("%s would not take %s (reads %r)" % (field, value, got))
        for s in screens:
            d.goto(s)
            st = d.state()
            out[s] = [r.get("text", "").rstrip() for r in st.get("rows", [])]
            if not out[s]:
                raise SystemExit("no rows decoded on %s" % s)
    return out


CAPTION = re.compile(r"\b([0-9A-F]{2})[A-Za-z][A-Za-z0-9%.:_-]*")


def norm(row):
    """Normalise a decoded row before comparing it with another read of itself.

    Two ways the same screen decodes differently, both of which discarded good
    runs before they were understood:

    1. **Spacing moves.** `m8drv` records one MIXER field arriving as
       `" OUTPUT VOL  F0"` and as `"OUTPUTVOLF0"`. A run with every value
       identical was thrown away because SHAPE rendered as `"SHAPE   06SINE"`
       and then `"SHAPE  06SINE"`.
    2. **An enum's caption comes and goes.** The value is drawn, the text beside
       it sometimes is not: `"LIM 00"` against `"LIM 00CLIP"`, `"FILTER 00"`
       against `"FILTER 00OFF"`. Three A3 captures were discarded for this
       before it was diagnosed -- the bytes were identical in every one.

    So: collapse space runs, then drop a caption that is attached to a hex value
    with no space. The value itself survives, so a real change still differs.

    Known limit, in the safe direction: a caption of more than one word
    (`"06PULSE 12%"`) that vanishes entirely still reads as drift, because only
    the attached first word is stripped. That costs a re-run, not a wrong number.
    """
    return CAPTION.sub(r"\1", " ".join(row.split()))


def diff(a, b):
    out = []
    for s in a:
        if len(a[s]) != len(b.get(s, [])):
            out.append("%s: row count %d -> %d" % (s, len(a[s]), len(b.get(s, []))))
            continue
        for i, (x, y) in enumerate(zip(a[s], b[s])):
            if norm(x) != norm(y):
                out.append("%s row %d: %r -> %r" % (s, i, x, y))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", required=True, help="output is <dir>/<tag>_<VALUE>.wav")
    ap.add_argument("--dir", default="hwtest_out/level")
    ap.add_argument("--screen", help="screen the swept field lives on")
    ap.add_argument("--field", help="field to sweep; omit to capture once as-is")
    ap.add_argument("--values", nargs="+", default=["-"], help="hex values for --field")
    ap.add_argument("--note", default="60")
    ap.add_argument("--vel", default="0x40")
    ap.add_argument("--seconds", type=float, default=2.0)
    ap.add_argument("--screens", default="INSTRUMENT,MIXER")
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--expect", action="append", default=[],
                    help="SCREEN:substring that must appear on that screen")
    a = ap.parse_args()

    screens = a.screens.split(",")
    os.makedirs(a.dir, exist_ok=True)
    failures = 0

    for v in a.values:
        setter = (a.screen, a.field, v) if a.field and v != "-" else None
        before = snapshot(screens, setter)
        bad = False
        for e in a.expect:
            scr, _, want = e.partition(":")
            if not any(want in row for row in before[scr]):
                print("rig: expected %r on %s at %s=%s, not found" % (want, scr, a.field, v))
                for row in before[scr]:
                    print("   ", row)
                bad = True
        if bad:
            return 2

        out = os.path.join(a.dir, "%s_%s.wav" % (a.tag, v))
        cap = subprocess.run([CAPTURE, "--port", a.port, "--audio", "M8",
                              "--keyjazz", a.note, "--keyjazz-vel", a.vel,
                              "--seconds", str(a.seconds), "--out", out],
                             capture_output=True, text=True,
                             timeout=int(a.seconds) + 180)
        if cap.returncode != 0 or not os.path.exists(out):
            sys.stderr.write(cap.stdout[-1200:] + cap.stderr[-1200:])
            print("capture failed at %s=%s" % (a.field, v))
            return 3

        moved = diff(before, snapshot(screens))
        if moved:
            os.replace(out, out + ".DRIFTED")
            print("DRIFTED at %s=%s -- discarded:" % (a.field, v))
            for m in moved:
                print("   ", m)
            failures += 1
        else:
            print("ok  %s=%-4s -> %s" % (a.field, v, out))

    if failures:
        return 2
    print("--- rig, unchanged throughout ---")
    for s in screens:
        for row in snapshot(screens)[s]:
            if row.strip():
                print("   %-10s %s" % (s, row))
    return 0


if __name__ == "__main__":
    sys.exit(main())
