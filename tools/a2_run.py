#!/usr/bin/env python3
"""Guarded PLAY-toggle captures for captures_backlog A2 (ModFX MOD FRQ -> Hz).

Same before-and-after discipline as `level_run.py`, but it drives the SEQUENCER
rather than keyjazz, because that is what makes A2 measurable at all.

**A sequenced note sustains; a keyjazz note does not.** `hw_findings.md` §UI-33
recorded that every note on this rig was about 480 ms and listed four things that
did not lengthen it. All four were tried on KEYJAZZ. With the transport playing a
phrase whose only note is on row 0, the playhead parks on that row and the note
is held: measured 8.06 s, dead flat to 0.0 dB across every 20 ms bin, on the
§UI-30 FM patch. That is the whole reason this file exists.

**PLAY is a toggle, so it is asserted, not pressed blind.** Every capture checks
`is_playing` is false before and false after; if the two presses did not pair,
the capture is not a data point and the run stops. `playhead_observable` is
checked first, since `is_playing` is meaningless on a form screen (m8drv.md).

    python tools/a2_run.py --values 20 40 80 C0 FF --tag wet --seconds 8

Exit 0 = every capture taken with the rig verified on both sides. 2 = the rig
moved or a field would not take. 3 = a capture failed. 4 = the transport was not
in the state the toggle needs.
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
STEP_CELL = os.path.join(REPO, "tools", "step_cell.py")

# Same caption rule as level_run.norm(), including the 2026-08-29 fix that a
# caption's tail must hold a non-hex character -- otherwise a glued pair of bare
# hex bytes ("E0E0") is eaten down to one.
CAPTION = re.compile(r"\b([0-9A-F]{2})[A-Za-z][A-Za-z0-9%.:_-]*")


def norm(row):
    out = []
    for tok in row.split():
        m = CAPTION.match(tok)
        ok = (m and any(c.isdigit() for c in m.group(1))
              and any(c not in "0123456789ABCDEFabcdef" for c in tok[len(m.group(1)):]))
        out.append(m.group(1) if ok else tok)
    return "".join(out)


def snapshot(screens):
    out = {}
    with M8Driver() as d:
        for s in screens:
            d.goto(s)
            st = d.state()
            out[s] = [r.get("text", "").rstrip() for r in st.get("rows", [])]
            if not out[s]:
                raise SystemExit("no rows decoded on %s" % s)
    return out


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


def transport():
    """(is_playing, observable), read on a GRID screen so the playhead is drawn."""
    with M8Driver() as d:
        d.goto("SONG")
        st = d.state()
        return st.get("is_playing"), st.get("playhead_observable")


def set_pair(half, value):
    """Set one half of the paired `MOD DEPTH:FRQ` cell. `half` is 'DEPTH' or 'FRQ'.

    Stepped rather than set, because `editValue` refuses a paired cell (rig
    fact 7). The navigation is the load-bearing part:

    **`cursor CHO_MOD_DEP` does not disambiguate the two halves.** The field map
    has one entry for the pair, so the driver considers the cursor already on the
    field when it sits on EITHER half and does not move it. Navigating by name
    alone therefore steps whichever half the cursor happened to be left on -- and
    it did: a sweep that meant to take FRQ from FF down to C0 drove DEPTH from FF
    to 00 instead, while reporting that it had failed to move anything.

    So the cursor is SATURATED LEFT and then counted right, which is the same
    idiom `fm_probe_map.py` needed for the FM screen. That is absolute rather
    than relative, so it does not matter where the cursor starts.
    """
    keys = ["LEFT", "LEFT"] + (["RIGHT"] if half == "FRQ" else [])
    rx = (r"DEPTH:FRQ +[0-9A-F]{2}:([0-9A-F]{2})" if half == "FRQ"
          else r"DEPTH:FRQ +([0-9A-F]{2}):")
    r = subprocess.run([sys.executable, STEP_CELL, "--screen", "EFFECTS",
                        "--from-field", "CHO_MOD_DEP", "--keys"] + keys +
                       ["--row-re", rx, "--to", value],
                       capture_output=True, text=True, timeout=900)
    sys.stdout.write(r.stdout[-400:])
    if r.returncode != 0 or "FAILED" in r.stdout:
        raise SystemExit("%s would not reach %s" % (half, value))


def set_frq(value):
    set_pair("FRQ", value)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", required=True)
    ap.add_argument("--dir", default="hwtest_out/a2")
    ap.add_argument("--values", nargs="+", default=["-"], help="hex MOD FRQ values")
    ap.add_argument("--seconds", type=float, default=8.0)
    ap.add_argument("--screens", default="INSTRUMENT,MIXER,EFFECTS")
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--expect", action="append", default=[])
    a = ap.parse_args()

    screens = a.screens.split(",")
    os.makedirs(a.dir, exist_ok=True)
    failures = 0

    for v in a.values:
        if v != "-":
            set_frq(v)

        playing, observable = transport()
        if not observable:
            print("playhead not observable -- is_playing cannot be trusted")
            return 4
        if playing:
            print("transport ALREADY PLAYING before %s -- refusing" % v)
            return 4

        before = snapshot(screens)
        bad = False
        for e in a.expect:
            scr, _, want = e.partition(":")
            if not any(norm(want) in norm(row) for row in before[scr]):
                print("rig: expected %r on %s at FRQ=%s, not found" % (want, scr, v))
                for row in before[scr]:
                    print("   ", row)
                bad = True
        if bad:
            return 2

        out = os.path.join(a.dir, "%s_%s.wav" % (a.tag, v))
        cap = subprocess.run([CAPTURE, "--port", a.port, "--audio", "M8",
                              "--seconds", str(a.seconds), "--out", out],
                             capture_output=True, text=True,
                             timeout=int(a.seconds) + 240)
        if cap.returncode != 0 or not os.path.exists(out):
            sys.stderr.write(cap.stdout[-1200:] + cap.stderr[-1200:])
            print("capture failed at FRQ=%s" % v)
            return 3

        playing, _ = transport()
        if playing:
            print("transport LEFT PLAYING after FRQ=%s -- the toggle did not pair, "
                  "so the window is not one continuous take" % v)
            os.replace(out, out + ".UNPAIRED")
            return 4

        moved = diff(before, snapshot(screens))
        if moved:
            os.replace(out, out + ".DRIFTED")
            print("DRIFTED at FRQ=%s -- discarded:" % v)
            for m in moved:
                print("   ", m)
            failures += 1
        else:
            print("ok  FRQ=%-4s -> %s" % (v, out))

    if failures:
        return 2
    print("--- rig, unchanged throughout ---")
    final = snapshot(screens)
    for s in screens:
        for row in final[s]:
            if row.strip():
                print("   %-11s %s" % (s, row))
    return 0


if __name__ == "__main__":
    sys.exit(main())
