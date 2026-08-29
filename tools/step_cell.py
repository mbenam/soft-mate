#!/usr/bin/env python3
"""Step a hex byte the field map cannot name, and read the row back each time.

Third time this pattern has been needed -- `set_rev_decay.py` for the reverb's
`DECAY:SHIMMER` pair, `fm_patch.py` for the whole FM screen, and now the mixer's
`OTT`, which has no entry in `kMixerFields` at all. So it is a tool.

Reach the cell by naming a field the map *does* know and then pressing your way
to it, and identify the value by a regex over the decoded ROW text rather than
by `cursor_value` -- which glues label to value and is what makes `editValue`
refuse a paired cell in the first place (rig fact 7).

    python tools/step_cell.py --screen MIXER --from-field DJF_FREQ --keys DOWN \\
        --row-re "OTT ([0-9A-F]{2})" --to 40
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
M8DRV = [sys.executable, os.path.join(REPO, "tools", "m8drv", "m8drv.py")]
KEYS = {"UP": "0x40", "DOWN": "0x20", "LEFT": "0x80", "RIGHT": "0x04"}
INC1, DEC1, INC16, DEC16 = "0x05", "0x81", "0x41", "0x21"


def batch(lines):
    p = os.path.join(REPO, "_stepcell.txt")
    open(p, "w").write("\n".join(lines) + "\n")
    r = subprocess.run(M8DRV + ["batch", p], capture_output=True, text=True, timeout=900)
    if r.returncode != 0:
        raise SystemExit("batch failed:\n" + r.stdout + r.stderr)


def read(pattern):
    r = subprocess.run(M8DRV + ["dump"], capture_output=True, text=True, timeout=180)
    m = re.search(pattern, r.stdout)
    if not m:
        raise SystemExit("row pattern %r not found in:\n%s" % (pattern, r.stdout))
    return int(m.group(1), 16)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--screen", required=True)
    ap.add_argument("--from-field", help="a field the map DOES know; omit on a screen "
                                        "with no field map at all (MODS), where GOTO "
                                        "already lands on a known cell")
    ap.add_argument("--keys", nargs="*", default=[], help="UP/DOWN/LEFT/RIGHT from there")
    ap.add_argument("--row-re", required=True, help="regex with one hex capture group")
    ap.add_argument("--to", required=True, help="target hex byte")
    ap.add_argument("--rounds", type=int, default=12)
    ap.add_argument("--enum", action="store_true",
                    help="step one at a time and reverse on a stall, for enum cells "
                         "whose lists do not wrap")
    a = ap.parse_args()

    target = int(a.to, 16)
    path = ["GOTO screen=%s" % a.screen]
    if a.from_field:
        path.append("CURSOR field=%s" % a.from_field)
    batch(path + ["PRESS key=%s" % KEYS[k.upper()] for k in a.keys])
    for _ in range(a.rounds):
        cur = read(a.row_re)
        if cur == target:
            print("%s = %02X" % (a.row_re, cur))
            return 0
        if a.enum:
            presses = ["PRESS key=%s" % (INC1 if target > cur else DEC1)]
            batch(presses)
            continue
        d = target - cur
        presses = []
        while abs(d) >= 16 and len(presses) < 20:
            presses.append("PRESS key=%s" % (INC16 if d > 0 else DEC16))
            d -= 16 if d > 0 else -16
        while d != 0 and len(presses) < 40:
            presses.append("PRESS key=%s" % (INC1 if d > 0 else DEC1))
            d -= 1 if d > 0 else -1
        batch(presses)
    print("FAILED to reach %02X, cell reads %02X" % (target, read(a.row_re)))
    return 2


if __name__ == "__main__":
    sys.exit(main())
