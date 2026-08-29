#!/usr/bin/env python3
"""Author the FMSYNTH patch for the PIT-modulation measurement, on the device.

For captures_backlog B1. `m8_makeprobe` can now bake this patch (`--fm-algo`,
`--fm-level`, `--fm-mod-a`, `--fm-mod-amt`), but loading a probe needs the file
on the SD card and there is no file transfer over serial -- so on this rig the
patch has to be built on the device instead.

`ScreenModel.h` has no FM field map and the FM screen's cursor coordinates do
not distinguish its cells: walking right along the LEV/FB row reports `(120, 8)`
at every stop. The paths below were measured by `fm_probe_map.py`, which presses
a value key and reads back **which number on the screen moved**. From a cursor
homed on TYPE, and saturating LEFT before counting RIGHT:

    DOWN*3 LEFT*2 RIGHT*0   ALGO
    DOWN*6 LEFT*4 RIGHT*n   n=0 LEV A, 1 FB A, 2 LEV B, 3 FB B,
                            4 LEV C, 5 FB C, 6 LEV D, 7 FB D
    DOWN*7 LEFT*5 RIGHT*n   MOD slot 1 of operator A/B/C/D
    DOWN*9 LEFT*2 RIGHT*0   MOD1 amount   (RIGHT*1 is AMP -- do not overshoot)

Every step is verified by re-reading the row, never by assuming a press landed.

    python tools/fm_patch.py --setup          # ALGO 0B, op A alone, MOD1 -> PIT
    python tools/fm_patch.py --mod1 80        # then sweep just the amount
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
M8DRV = [sys.executable, os.path.join(REPO, "tools", "m8drv", "m8drv.py")]
DOWN, RIGHT, LEFT = "0x20", "0x04", "0x80"
INC1, DEC1, INC16, DEC16 = "0x05", "0x81", "0x41", "0x21"


def batch(lines):
    p = os.path.join(REPO, "_fmpatch.txt")
    open(p, "w").write("\n".join(lines) + "\n")
    r = subprocess.run(M8DRV + ["batch", p], capture_output=True, text=True, timeout=900)
    if r.returncode != 0:
        raise SystemExit("batch failed:\n" + r.stdout + r.stderr)


def rows():
    r = subprocess.run(M8DRV + ["dump"], capture_output=True, text=True, timeout=180)
    out = {}
    for line in r.stdout.splitlines():
        m = re.match(r"\s*y=(\d+)\s*(.*?)\s*$", line)
        if m:
            out[int(m.group(1))] = m.group(2)
    if 120 not in out:
        raise SystemExit("not on the FM instrument screen:\n" + r.stdout)
    return out


def goto(down, left, right):
    batch(["CURSOR field=TYPE"] + ["PRESS key=%s" % DOWN] * down +
          ["PRESS key=%s" % LEFT] * left + ["PRESS key=%s" % RIGHT] * right)


def algo(rs):
    m = re.match(r"ALGO\s+([0-9A-F]{2})(.*)", rs[90])
    return int(m.group(1), 16), m.group(2)


def levfb(rs, i):
    p = re.findall(r"([0-9A-F]{2})/([0-9A-F]{2})", rs[120])
    return int(p[i][0], 16), int(p[i][1], 16)


def modslot(rs, i):
    return rs[130].replace("MOD", "", 1).split()[i]


def mod1(rs):
    return int(re.match(r"MOD1\s+([0-9A-F]{2})", rs[150]).group(1), 16)


def step_byte(read, target, label, limit=14):
    """Drive a hex byte to target with +-16 / +-1, re-reading after each batch."""
    for _ in range(limit):
        cur = read(rows())
        if cur == target:
            return cur
        d = target - cur
        presses = []
        while abs(d) >= 16 and len(presses) < 20:
            presses.append("PRESS key=%s" % (INC16 if d > 0 else DEC16))
            d -= 16 if d > 0 else -16
        while d != 0 and len(presses) < 40:
            presses.append("PRESS key=%s" % (INC1 if d > 0 else DEC1))
            d -= 1 if d > 0 else -1
        batch(presses)
    raise SystemExit("%s would not reach %02X (stuck at %02X)" % (label, target, read(rows())))


def step_enum(read, want, label, limit=24):
    """Step an enum to `want`, reversing when the list saturates.

    These lists do not wrap: stepping forward from `1>PIT` runs up to `4>FBK`
    and stops there, so a one-directional walk can never reach `-----` again.
    Reverse on a stall rather than assuming a direction."""
    key = INC1
    last = None
    for _ in range(limit):
        cur = read(rows())
        if cur == want:
            return want
        if cur == last:
            key = DEC1 if key == INC1 else INC1
        last = cur
        batch(["PRESS key=%s" % key])
    raise SystemExit("%s would not reach %r (stuck at %r)" % (label, want, read(rows())))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--setup", action="store_true")
    ap.add_argument("--mod1", help="set the MOD1 amount to this hex byte")
    ap.add_argument("--mod-a1", help="set operator A's first MOD slot to this text, "
                                     "e.g. 1>PIT or ----- (the control)")
    ap.add_argument("--show", action="store_true")
    a = ap.parse_args()

    if a.setup:
        goto(3, 2, 0)
        step_enum(lambda rs: algo(rs)[0], 0x0B, "ALGO")
        print("ALGO %02X%s" % algo(rows()))

        goto(6, 4, 0)
        step_byte(lambda rs: levfb(rs, 0)[0], 0xFF, "LEV A")
        for op, n in ((1, 2), (2, 4), (3, 6)):
            goto(6, 4, n)
            step_byte(lambda rs, o=op: levfb(rs, o)[0], 0x00, "LEV %s" % "ABCD"[op])
        print("LEV/FB %s" % rows()[120])

        goto(7, 5, 0)
        step_enum(lambda rs: modslot(rs, 0), "1>PIT", "MOD slot A1")
        print("MOD    %s" % rows()[130])

    if a.mod_a1:
        goto(7, 5, 0)
        step_enum(lambda rs: modslot(rs, 0), a.mod_a1, "MOD slot A1")

    if a.mod1:
        goto(9, 2, 0)
        step_byte(mod1, int(a.mod1, 16), "MOD1")

    rs = rows()
    print("ALGO %02X%s | RATIO %s | LEV/FB %s | MOD %s | MOD1 %02X" %
          (algo(rs)[0], algo(rs)[1], rs[110].split(None, 1)[1], rs[120].split(None, 1)[1],
           rs[130].split(None, 1)[1], mod1(rs)))
    for y in (180, 190, 200, 210):
        print("   %s" % rs[y])
    return 0


if __name__ == "__main__":
    sys.exit(main())
