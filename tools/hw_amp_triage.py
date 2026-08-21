#!/usr/bin/env python3
"""Triage AMP's effect across every LIM mode, on hardware.

Why a script and not a batch file
---------------------------------
Two things make this awkward by hand. `set LIM` does not converge -- the field
is an enum and editValue cannot reach it -- so LIM has to be stepped with
EDIT+RIGHT (0x05) presses and read back each time. And every capture has to go
through `hw_measure.py`, which verifies the state before AND after, because a
`set AMP` was once observed to move LIM silently (M8_DRIVER_BUGS.md #34).

What it does
------------
Floors LIM to 00, then for each LIM mode captures AMP 00 and AMP FF and reports
peak / crest for each. That is 2*9 = 18 captures and answers the only question
worth asking first: WHICH modes does AMP affect at all. Sweeping a curve in a
mode where AMP does nothing is how the last three attempts were wasted.

Preconditions (checked, not assumed)
------------------------------------
* A WavSynth probe at instrument volume 0x7F is loaded -- WAVV7F.M8S. TYPE is
  verified on every capture; the level is checked on the first one, because a
  quiet capture measures noise and reads as a shallow curve (peak ~0.36 is what
  0x7F gives; ~0.03 is what killed the earlier attempts).
* Nothing else holds COM3.

    python tools/hw_amp_triage.py                  # all 9 modes
    python tools/hw_amp_triage.py --lims 0,4,8     # a subset
"""
from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
M8DRV = os.path.join(ROOT, "tools", "m8drv", "m8drv.py")
HW_MEASURE = os.path.join(ROOT, "tools", "hw_measure.py")
ANALYZE = os.path.join(ROOT, "build", "Release", "m8_analyze.exe")
OUTDIR = os.path.join(ROOT, "hwtest_out", "triage")

LIM_NAMES = ["CLIP", "SIN", "FOLD", "WRAP", "POST",
             "POST:AD", "POST:W1", "POST:W2", "POST:W3"]

KEY_LIM_UP = "0x05"    # EDIT+RIGHT, +1 on an enum
KEY_LIM_DOWN = "0x81"  # EDIT+LEFT,  -1


def drv(lines, timeout=240):
    p = subprocess.run([sys.executable, M8DRV, "batch"],
                       input="\n".join(lines) + "\n",
                       capture_output=True, text=True, cwd=ROOT, timeout=timeout)
    return p.stdout + p.stderr


def read_value(out, field):
    """A field's value out of a batch transcript, e.g. '04POST'.

    Only [ok ] lines count. A failed READ still echoes the row it was last on,
    so an [ERR] line parses as a perfectly plausible value -- that is how a
    HyperSynth, which draws no AMP row, reported its TYPE as its AMP.
    """
    for line in out.splitlines():
        if not line.lstrip().startswith("[ok ]"):
            continue
        if "READ field=%s" % field in line and "= '" in line:
            return line.split("= '", 1)[1].split("'", 1)[0].strip()
    return None


def read_lim(out):
    return read_value(out, "LIM")


def press_lim(key, times=1):
    """Press a LIM step key N times and return the value read back after."""
    lines = ["GOTO screen=INSTRUMENT", "CURSOR field=LIM"]
    lines += ["PRESS key=%s" % key] * times
    lines += ["READ field=LIM"]
    return read_lim(drv(lines))


def norm_has(got, want):
    return got is not None and got.strip().upper().replace(" ", "").startswith(want.upper())


def at(got, target):
    return got is not None and got.upper().replace(" ", "").startswith("%02X" % target)


def goto_lim(target):
    """Reach a LIM mode by presses, verifying by read-back at every step.

    Always approaches from below: the floor is a run of -1 presses (+-16 clamps
    on a 0..8 enum, so the coarse keys are useless here), then +1 at a time.
    Absolute, not relative, so a caller can ask for the modes in any order.
    """
    got = press_lim(KEY_LIM_DOWN, 12)
    if not at(got, 0):
        print("  LIM would not floor to 00 (reads %r)" % got)
        return False
    for step in range(1, target + 1):
        got = press_lim(KEY_LIM_UP)
        if not at(got, step):
            print("  LIM would not step to %02X (reads %r)" % (step, got))
            return False
    return True


def measure(path, amp, lim, seconds, inst_type):
    """One verified capture. Returns hw_measure's exit code."""
    cmd = [sys.executable, HW_MEASURE, "--out", path,
           "--set", "AMP=%s" % amp,
           "--require", "LIM=%02X" % lim,
           "--require", "TYPE=%s" % inst_type,
           "--seconds", str(seconds)]
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT, timeout=600)
    for line in (p.stdout + p.stderr).splitlines():
        print("    " + line.rstrip())
    return p.returncode


def analyze(path):
    """peak / rms / crest_db for a captured wav, or None."""
    jp = os.path.splitext(path)[0] + ".json"
    p = subprocess.run([ANALYZE, path, "--json", jp],
                       capture_output=True, text=True, cwd=ROOT, timeout=120)
    if not os.path.exists(jp):
        print("    analyze failed rc=%d: %s" % (p.returncode, p.stderr.strip()[:200]))
        return None
    with open(jp) as f:
        return json.load(f)["metrics"]


def mean(xs):
    return sum(xs) / len(xs) if xs else 0.0


def spread_db(xs):
    """Peak-to-peak spread of repeats, in dB. Zero for a single measurement."""
    if len(xs) < 2 or min(xs) <= 0:
        return 0.0
    return 20.0 * math.log10(max(xs) / min(xs))


def db(a, b):
    if not a or not b or a <= 0 or b <= 0:
        return None
    return 20.0 * math.log10(b / a)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lims", default="0,1,2,3,4,5,6,7,8")
    ap.add_argument("--amps", default="00,FF")
    ap.add_argument("--seconds", default="3")
    ap.add_argument("--min-peak", type=float, default=0.20,
                    help="abort if the first capture is quieter than this")
    ap.add_argument("--load", default=None,
                    help="project to load before measuring, e.g. WAVV7F")
    ap.add_argument("--type", default="WAVSYNTH",
                    help="instrument TYPE the probe must report")
    ap.add_argument("--reps", type=int, default=1,
                    help="measurements per point; >1 gives a spread to judge "
                         "a small difference against")
    ap.add_argument("--retries", type=int, default=3,
                    help="attempts when a capture comes back with no note in it")
    a = ap.parse_args()

    lims = [int(x) for x in a.lims.split(",") if x.strip() != ""]
    amps = [x.strip().upper() for x in a.amps.split(",") if x.strip() != ""]
    os.makedirs(OUTDIR, exist_ok=True)

    print("== preflight ==")
    if a.load:
        print(drv(["LOAD path=%s" % a.load], timeout=400).strip())
    out = drv(["GOTO screen=INSTRUMENT", "READ field=TYPE",
               "READ field=AMP", "READ field=LIM"])
    print(out.strip())
    typ, amp = read_value(out, "TYPE"), read_value(out, "AMP")
    # Refuse before the 18 captures, not during them. On 2026-08-20 the wrong
    # project was loaded (a HyperSynth, which has no AMP row at all) and the
    # run ground through nine modes producing nothing but refusals.
    if not norm_has(typ, a.type):
        print("ABORT: instrument TYPE reads %r, wanted %r. Load the right probe "
              "(--load WAVV7F) and re-run." % (typ, a.type))
        return 2
    if amp is None:
        print("ABORT: AMP is not readable on this instrument. Nothing to sweep.")
        return 2

    rows = {}
    first = True
    for lim in lims:
        print("== LIM %02X %s ==" % (lim, LIM_NAMES[lim] if lim < 9 else "?"))
        if not goto_lim(lim):
            rows[lim] = {}
            continue
        rows[lim] = {}
        for amp in amps:
            got = []
            for r in range(a.reps):
                path = os.path.join(OUTDIR, "L%02X_A%s_r%d.wav" % (lim, amp, r))
                m = None
                # rc 4 is "the capture holds no note" -- a transient PLAY-toggle
                # desync, not a property of the setting. Retry it; do NOT retry
                # rc 2, which means the device is not in the state we asked for.
                for attempt in range(a.retries):
                    rc = measure(path, amp, lim, a.seconds, a.type)
                    if rc == 0:
                        m = analyze(path)
                        break
                    print("    rc=%d on attempt %d" % (rc, attempt + 1))
                    if rc != 4:
                        break
                if m is None:
                    print("    no data point for LIM %02X AMP %s rep %d" % (lim, amp, r))
                    continue
                got.append(m)
                print("    r%d  peak %.4f  rms %.5f  crest %.2f dB" %
                      (r, m["peak"], m["rms"], m["crest_db"]))
            if not got:
                continue
            rows[lim][amp] = got
            m = got[0]
            if first:
                first = False
                if m["peak"] < a.min_peak:
                    print("ABORT: first capture peaked at %.4f, below --min-peak %.2f. "
                          "That is a noise-level measurement -- load WAVV7F.M8S "
                          "(instrument volume 0x7F) and re-run." % (m["peak"], a.min_peak))
                    return 2

    print()
    print("== AMP %s -> %s, per LIM mode ==" % (amps[0], amps[-1]))
    print("LIM  mode      peak(%s)  peak(%s)  dPeak     rms(%s)   rms(%s)   dRMS      "
          "crest %s->%s  spread" % (amps[0], amps[-1], amps[0], amps[-1],
                                    amps[0], amps[-1]))
    for lim in lims:
        r = rows.get(lim, {})
        lo, hi = r.get(amps[0]), r.get(amps[-1])
        if not lo or not hi:
            print("%02X   %-9s  (incomplete)" % (lim, LIM_NAMES[lim]))
            continue
        lo_p = mean([m["peak"] for m in lo])
        hi_p = mean([m["peak"] for m in hi])
        lo_r = mean([m["rms"] for m in lo])
        hi_r = mean([m["rms"] for m in hi])
        # The spread across repeats of one setting is the yardstick: a delta
        # smaller than the spread is not a measurement of anything.
        sp = max(spread_db([m["peak"] for m in lo]), spread_db([m["peak"] for m in hi]))
        print("%02X   %-9s  %7.4f   %7.4f   %+7.2f dB  %7.5f  %7.5f  %+7.2f dB  "
              "%5.2f -> %5.2f  +-%.2f (n=%d/%d)"
              % (lim, LIM_NAMES[lim], lo_p, hi_p, db(lo_p, hi_p),
                 lo_r, hi_r, db(lo_r, hi_r),
                 mean([m["crest_db"] for m in lo]), mean([m["crest_db"] for m in hi]),
                 sp, len(lo), len(hi)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
