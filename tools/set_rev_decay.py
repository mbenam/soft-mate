#!/usr/bin/env python3
"""Step the reverb DECAY byte to a target, by reading it back after every press.

`m8drv set REV_DEC` cannot do this. The EFFECT SETTINGS screen draws DECAY and
SHIMMER as one row, `DECAY:SHIMMER C0:00`, and `readCursorValue` returns the
accent-coloured cells glued together as `:SHIMMERC0` -- which has no leading hex
run, so `editValue` refuses (correctly) rather than stepping blind. One attempt
to force it timed out mid-sweep and left DECAY on `45`; that is exactly the
failure the refusal exists to prevent, and it is why this steps and re-reads
instead.

Reads the value out of the decoded ROW text, which is unambiguous, and drives it
with the pinned gestures from `hw_buttons.json` (EDIT+UP = +16, EDIT+DOWN = -16,
EDIT+RIGHT = +1, EDIT+LEFT = -1).

    python tools/set_rev_decay.py C0
"""
from __future__ import annotations

import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
M8DRV = [sys.executable, os.path.join(REPO, "tools", "m8drv", "m8drv.py")]

INC16, DEC16, INC1, DEC1 = "0x41", "0x21", "0x05", "0x81"
ROW = re.compile(r"DECAY:SHIMMER\s+([0-9A-F]{2}):([0-9A-F]{2})")


def read_decay():
    r = subprocess.run(M8DRV + ["dump"], capture_output=True, text=True, timeout=180)
    m = ROW.search(r.stdout)
    if not m:
        raise SystemExit("could not read DECAY row:\n" + r.stdout + r.stderr)
    return int(m.group(1), 16), int(m.group(2), 16)


def batch(lines):
    p = os.path.join(REPO, "_setdec.txt")
    open(p, "w").write("\n".join(lines) + "\n")
    r = subprocess.run(M8DRV + ["batch", p], capture_output=True, text=True, timeout=600)
    if r.returncode != 0:
        raise SystemExit("batch failed:\n" + r.stdout + r.stderr)


def main():
    target = int(sys.argv[1], 16)
    batch(["GOTO screen=EFFECTS", "CURSOR field=REV_DEC"])
    for attempt in range(12):
        cur, shimmer = read_decay()
        if cur == target:
            print("REV DECAY = %02X (SHIMMER %02X)" % (cur, shimmer))
            return 0
        d = target - cur
        presses = []
        while abs(d) >= 16 and len(presses) < 24:
            presses.append("PRESS key=%s" % (INC16 if d > 0 else DEC16))
            d -= 16 if d > 0 else -16
        while d != 0 and len(presses) < 40:
            presses.append("PRESS key=%s" % (INC1 if d > 0 else DEC1))
            d -= 1 if d > 0 else -1
        if not presses:
            break
        batch(presses)
    cur, shimmer = read_decay()
    print("FAILED to reach %02X, DECAY is %02X" % (target, cur))
    return 2


if __name__ == "__main__":
    sys.exit(main())
