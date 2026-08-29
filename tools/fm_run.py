#!/usr/bin/env python3
"""One guarded FM capture: set MOD1, assert the patch, capture, assert it again.

For captures_backlog B1. Same discipline as `rt60_run.py` (whose screen snapshot
and diff it reuses), pointed at the FM instrument screen instead of the reverb.
The whole measurement is a pitch read, so the thing that must not move is the
patch: ALGO, the four operator LEV/FB pairs, the MOD routing, the MOD1 amount
and the send routing all live on that one screen, and the MIXER carries the
returns that would colour the capture if they were open.

    python tools/fm_run.py --out hwtest_out/fm/pit80.wav --mod1 80

Exit 0 = captured and nothing moved. 2 = the patch would not take or drifted
(WAV renamed .DRIFTED). 3 = capture failed.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rt60_run  # noqa: E402

REPO = rt60_run.REPO
rt60_run.SCREENS = ("INSTRUMENT", "MIXER")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--mod1", help="set the MOD1 amount to this hex byte first")
    ap.add_argument("--seconds", type=float, default=3.0)
    ap.add_argument("--keyjazz", default="60")
    ap.add_argument("--vel", default="0x40")
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--expect", action="append", default=[])
    a = ap.parse_args()

    if a.mod1:
        r = subprocess.run([sys.executable, os.path.join(REPO, "tools", "fm_patch.py"),
                            "--mod1", a.mod1], capture_output=True, text=True, timeout=900)
        sys.stdout.write(r.stdout + r.stderr)
        if r.returncode != 0:
            print("rig: MOD1 would not take")
            return 2

    before = rt60_run.snapshot()
    for e in a.expect:
        scr, _, want = e.partition(":")
        if not any(want in row for row in before[scr]):
            print("rig: expected %r on %s, not found:" % (want, scr))
            for row in before[scr]:
                print("   ", row)
            return 2

    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    cap = subprocess.run([rt60_run.CAPTURE, "--port", a.port, "--audio", "M8",
                          "--keyjazz", a.keyjazz, "--keyjazz-vel", a.vel,
                          "--seconds", str(a.seconds), "--out", a.out],
                         capture_output=True, text=True, timeout=int(a.seconds) + 180)
    sys.stdout.write(cap.stdout[-1500:])
    if cap.returncode != 0 or not os.path.exists(a.out):
        print("capture failed")
        return 3

    moved = rt60_run.diff(before, rt60_run.snapshot())
    if moved:
        os.replace(a.out, a.out + ".DRIFTED")
        print("RIG DRIFTED during capture -- run discarded:")
        for m in moved:
            print("   ", m)
        return 2

    print("rig held: %s" % a.out)
    for row in before["INSTRUMENT"]:
        print("   ", row)
    return 0


if __name__ == "__main__":
    sys.exit(main())
