#!/usr/bin/env python3
"""Map the FMSYNTH instrument screen by making each cell name itself.

`ScreenModel.h` has no FM field map, and the FM screen's reported cursor
coordinates do not distinguish its cells -- a walk right along the LEV/FB row
reports `(120, 8)` at every stop. So position reads cannot map this screen.

What can: press a value key and see **which number on the screen moved**. That
is unambiguous, it is the same trick rig fact 5 uses to read a table playhead,
and it needs nothing the driver does not already do. Each probe increments by
+16, dumps, decrements by -16, and dumps again to prove it went back.

    python tools/fm_probe_map.py --down 6 --right 0 1 2 3 4 5 6 7
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
M8DRV = [sys.executable, os.path.join(REPO, "tools", "m8drv", "m8drv.py")]
DOWN, RIGHT, LEFT, INC16, DEC16 = "0x20", "0x04", "0x80", "0x41", "0x21"


def batch(lines):
    p = os.path.join(REPO, "_fmmap.txt")
    open(p, "w").write("\n".join(lines) + "\n")
    r = subprocess.run(M8DRV + ["batch", p], capture_output=True, text=True, timeout=900)
    if r.returncode != 0:
        raise SystemExit("batch failed:\n" + r.stdout + r.stderr)


def fm_rows():
    r = subprocess.run(M8DRV + ["dump"], capture_output=True, text=True, timeout=180)
    rows = {}
    for line in r.stdout.splitlines():
        m = re.match(r"\s*y=(\d+)\s*(.*?)\s*$", line)
        if m:
            rows[int(m.group(1))] = m.group(2)
    return rows


def diff(a, b):
    return ["y=%d: %r -> %r" % (k, a[k], b[k]) for k in sorted(a) if a.get(k) != b.get(k)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--down", type=int, required=True)
    ap.add_argument("--right", type=int, nargs="+", required=True)
    ap.add_argument("--left", type=int, default=0,
                    help="press LEFT this many times after the DOWN run, before RIGHT")
    ap.add_argument("--key", default=INC16)
    ap.add_argument("--undo", default=DEC16)
    a = ap.parse_args()

    for k in a.right:
        path = ["CURSOR field=TYPE"] + ["PRESS key=%s" % DOWN] * a.down + \
               ["PRESS key=%s" % LEFT] * a.left + ["PRESS key=%s" % RIGHT] * k
        batch(path)
        before = fm_rows()
        batch(["PRESS key=%s" % a.key])
        after = fm_rows()
        moved = diff(before, after)
        batch(["PRESS key=%s" % a.undo])
        back = fm_rows()
        restored = not diff(before, back)
        print("DOWN*%d LEFT*%d RIGHT*%d -> %s%s" %
              (a.down, a.left, k, "; ".join(moved) if moved else "NOTHING MOVED",
               "" if restored else "   !! DID NOT RESTORE: " + "; ".join(diff(before, back))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
