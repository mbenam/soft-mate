#!/usr/bin/env python3
"""One guarded reverb capture: assert the rig, capture, assert the rig again.

Written for captures_backlog A1 (reverb DECAY -> RT60). It is `hw_measure.py`'s
before-and-after discipline applied to three screens instead of one, because the
reverb measurement depends on state on INSTRUMENT (the sends), EFFECT SETTINGS
(the reverb block) and MIXER (the returns), and `hw_measure.py` can only read
INSTRUMENT.

The comparison is on decoded row TEXT, per m8drv.md -- never on the semantic
state blob, whose `settled` flag flaps between reads whether or not anything
moved.

    python tools/rt60_run.py --out hwtest_out/rev/decC0.wav --dec C0 --seconds 15

Exit 0 = captured and nothing moved. Exit 2 = the rig would not take, or it
drifted during the capture (the WAV is renamed .DRIFTED). Exit 3 = capture
failed.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
M8DRV = [sys.executable, os.path.join(REPO, "tools", "m8drv", "m8drv.py")]
CAPTURE = os.path.join(REPO, "build", "Release", "m8_capture.exe")

SCREENS = ("INSTRUMENT", "EFFECTS", "MIXER")


def dump_rows(screen):
    """Decoded row text for one screen, as a list of 'y=NNN <text>' lines."""
    subprocess.run(M8DRV + ["goto", screen], capture_output=True, text=True, timeout=180)
    r = subprocess.run(M8DRV + ["dump"], capture_output=True, text=True, timeout=180)
    rows = []
    for line in r.stdout.splitlines():
        st = line.strip()
        if st.startswith("y="):
            rows.append(st.rstrip())
    if not rows:
        raise SystemExit("dump %s: no rows decoded\n%s" % (screen, r.stdout + r.stderr))
    return rows


def snapshot():
    return {s: dump_rows(s) for s in SCREENS}


def diff(a, b):
    out = []
    for s in SCREENS:
        for i, (x, y) in enumerate(zip(a[s], b[s])):
            if x != y:
                out.append("%s row %d: %r -> %r" % (s, i, x, y))
        if len(a[s]) != len(b[s]):
            out.append("%s: row count %d -> %d" % (s, len(a[s]), len(b[s])))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--dec", help="set REVERB DECAY to this hex byte first")
    ap.add_argument("--seconds", type=float, default=15.0)
    ap.add_argument("--note-ms", type=float, default=300.0)
    ap.add_argument("--keyjazz", default="60")
    ap.add_argument("--vel", default="0x40")
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--expect", action="append", default=[],
                    help="SCREEN:substring that must appear somewhere in that screen's rows")
    a = ap.parse_args()

    if a.dec:
        # Not `m8drv set REV_DEC` -- see tools/set_rev_decay.py for why that
        # cannot work on the DECAY:SHIMMER row, and for the run it corrupted.
        r = subprocess.run([sys.executable, os.path.join(REPO, "tools", "set_rev_decay.py"), a.dec],
                           capture_output=True, text=True, timeout=900)
        sys.stdout.write(r.stdout + r.stderr)
        if r.returncode != 0:
            print("rig: REV DECAY would not take")
            return 2

    before = snapshot()
    for e in a.expect:
        scr, _, want = e.partition(":")
        if not any(want in row for row in before[scr]):
            print("rig: expected %r on %s, not found:" % (want, scr))
            for row in before[scr]:
                print("   ", row)
            return 2

    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    cap = subprocess.run([CAPTURE, "--port", a.port, "--audio", "M8",
                          "--keyjazz", a.keyjazz, "--keyjazz-vel", a.vel,
                          "--note-ms", str(a.note_ms),
                          "--seconds", str(a.seconds), "--out", a.out],
                         capture_output=True, text=True, timeout=int(a.seconds) + 180)
    sys.stdout.write(cap.stdout[-2000:])
    sys.stderr.write(cap.stderr[-2000:])
    if cap.returncode != 0 or not os.path.exists(a.out):
        print("capture failed")
        return 3

    after = snapshot()
    moved = diff(before, after)
    if moved:
        os.replace(a.out, a.out + ".DRIFTED")
        print("RIG DRIFTED during capture -- run discarded:")
        for m in moved:
            print("   ", m)
        return 2

    print("rig held: %s" % a.out)
    for s in SCREENS:
        print("--- %s ---" % s)
        for row in before[s]:
            print("   ", row)
    return 0


if __name__ == "__main__":
    sys.exit(main())
