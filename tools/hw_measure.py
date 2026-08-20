#!/usr/bin/env python3
"""Capture audio from the device with every dependent field verified by read-back.

Why this exists
---------------
A hardware measurement is only worth the state it was taken in, and on
2026-08-19 an AMP sweep produced a data point taken at the wrong LIM setting:
`set AMP FF` silently moved LIM from 04 to 08 (M8_DRIVER_BUGS.md #34). Nothing
in the run said so. The number looked exactly like a real measurement and had to
be thrown away only because a later read happened to notice.

The lesson is not "fix the driver" -- that is #34's guard, and it makes drift
loud rather than impossible. The lesson is that a measurement harness must
re-read every field its result depends on, immediately before AND after the
capture, and refuse the capture if either read disagrees. Setting a field and
believing it is what went wrong; verifying twice costs two serial reads.

It also verifies AFTER, not just before, because the capture itself presses PLAY
-- and a press is exactly the thing suspected of moving the cursor in #34.

Usage
-----
    python tools/hw_measure.py --out hwtest_out/x.wav --set AMP=00 --set LIM=04
    python tools/hw_measure.py --out hwtest_out/x.wav --set AMP=FF --require TYPE=WAVSYNTH

`--set F=V` sets and then verifies. `--require F=V` only verifies -- for state
you did not set but are relying on, such as the loaded instrument type.

Exit codes: 0 captured and verified, 2 a field would not take or drifted (no
capture kept), 3 the capture tool itself failed.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
M8DRV = os.path.join(ROOT, "tools", "m8drv", "m8drv.py")
CAPTURE = os.path.join(ROOT, "build", "Release", "m8_capture.exe")


def drv(lines, timeout=200):
    """Run a batch through m8drv; return its stdout."""
    p = subprocess.run([sys.executable, M8DRV, "batch"],
                       input="\n".join(lines) + "\n",
                       capture_output=True, text=True, cwd=ROOT, timeout=timeout)
    return p.stdout


def read_field(name):
    """The field's value as the device currently shows it, or None."""
    out = drv(["GOTO screen=INSTRUMENT", "READ field=%s" % name])
    for line in out.splitlines():
        if "READ field=%s" % name in line and "->" in line:
            # "[ok ] READ field=AMP  -> AMP 40  = '40'  (row ...)"
            if "= '" in line:
                return line.split("= '", 1)[1].split("'", 1)[0].strip()
    return None


def norm(v):
    """Compare case- and padding-insensitively; the device pads its labels."""
    return (v or "").strip().upper().replace(" ", "")


def matches(got, want):
    """The device shows enums as VALUE+TEXT, e.g. '04POST' for a wanted '04'."""
    g, w = norm(got), norm(want)
    return g == w or g.startswith(w)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--set", action="append", default=[], metavar="FIELD=VALUE")
    ap.add_argument("--require", action="append", default=[], metavar="FIELD=VALUE")
    ap.add_argument("--seconds", default="3")
    ap.add_argument("--keyjazz", default=None, help="MIDI note; omit to use PLAY")
    ap.add_argument("--keyjazz-vel", default="0x40")
    a = ap.parse_args()

    def split(pairs):
        out = []
        for p in pairs:
            if "=" not in p:
                print("bad pair %r, want FIELD=VALUE" % p, file=sys.stderr)
                sys.exit(2)
            f, v = p.split("=", 1)
            out.append((f.strip(), v.strip()))
        return out

    to_set = split(a.set)
    to_req = split(a.require)

    # 1. Set what we were asked to set.
    for f, v in to_set:
        drv(["GOTO screen=INSTRUMENT", "SET field=%s value=%s" % (f, v)])

    # 2. Verify EVERYTHING before capturing. A field that would not take is a
    #    refused measurement, not a warning -- the whole point is that a wrong
    #    number is worse than a missing one.
    for f, v in to_set + to_req:
        got = read_field(f)
        if not matches(got, v):
            print("REFUSED: %s reads %r, wanted %r -- not capturing" % (f, got, v),
                  file=sys.stderr)
            sys.exit(2)
    before = {f: read_field(f) for f, _ in to_set + to_req}

    # 3. Capture.
    cmd = [CAPTURE, "--port", "COM3", "--out", a.out, "--seconds", a.seconds]
    if a.keyjazz is not None:
        cmd += ["--keyjazz", a.keyjazz, "--keyjazz-vel", a.keyjazz_vel]
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT, timeout=180)
    sys.stdout.write("".join(l + "\n" for l in p.stdout.splitlines()
                             if "capture peak" in l or "wrote" in l))
    if p.returncode != 0:
        print("capture failed rc=%d" % p.returncode, file=sys.stderr)
        sys.exit(3)

    # 4. Verify again. The capture presses PLAY, and a press is exactly what is
    #    suspected of moving the cursor in #34 -- so state checked only
    #    beforehand proves nothing about the audio that was just recorded.
    drifted = []
    for f, v in to_set + to_req:
        got = read_field(f)
        if not matches(got, v):
            drifted.append("%s: %r -> %r (wanted %r)" % (f, before.get(f), got, v))
    if drifted:
        bad = a.out + ".DRIFTED"
        try:
            os.replace(a.out, bad)
        except OSError:
            bad = a.out
        print("REFUSED: state drifted during capture -- " + "; ".join(drifted),
              file=sys.stderr)
        print("  the wav was renamed to %s so it cannot be mistaken for data" % bad,
              file=sys.stderr)
        sys.exit(2)

    print("verified: " + ", ".join("%s=%s" % (f, before.get(f)) for f, _ in to_set + to_req))
    return 0


if __name__ == "__main__":
    sys.exit(main())
